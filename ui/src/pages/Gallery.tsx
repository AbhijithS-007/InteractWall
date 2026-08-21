import { useState, useRef, useEffect } from 'react';
import { open } from '@tauri-apps/plugin-dialog';
import { load } from '@tauri-apps/plugin-store';
import { convertFileSrc } from '@tauri-apps/api/core';
import { importWallpaper, saveBakedWallpaper, deleteBakedWallpaper, applyWallpaper, listWallpapers, removeEffect, clearWallpaper } from '../ipc';
import { saveActiveSession } from '../store';
import { Plus, X, Image as ImageIcon, Save, Upload, Check, Undo2, Redo2, FolderOpen, Trash2, Info } from 'lucide-react';

interface Transform {
  panX: number;
  panY: number;
  zoom: number;
  rotate: number;
}

interface Shape {
  id: string;
  vertices: [number, number][];
  imagePath: string | null;
  transform: Transform;
}

interface Background {
  type: 'color' | 'image';
  value: string;
  fit: 'cover' | 'contain' | 'stretch' | 'tile';
}

interface Project {
  version: string;
  background: Background;
  shapes: Shape[];
}

const DEFAULT_PROJECT: Project = {
  version: "1.0",
  background: { type: 'color', value: '#1a1a1a', fit: 'cover' },
  shapes: []
};

// Simple utility to check if a point is inside a polygon
function isPointInPolygon(point: [number, number], vs: [number, number][]) {
  let x = point[0], y = point[1];
  let inside = false;
  for (let i = 0, j = vs.length - 1; i < vs.length; j = i++) {
    let xi = vs[i][0], yi = vs[i][1];
    let xj = vs[j][0], yj = vs[j][1];
    let intersect = ((yi > y) !== (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi);
    if (intersect) inside = !inside;
  }
  return inside;
}

// Compute center of a polygon (bounding box center)
function getPolygonCenter(vs: [number, number][]): [number, number] {
  if (vs.length === 0) return [0, 0];
  let minX = vs[0][0], maxX = vs[0][0], minY = vs[0][1], maxY = vs[0][1];
  for (let i = 1; i < vs.length; i++) {
    minX = Math.min(minX, vs[i][0]);
    maxX = Math.max(maxX, vs[i][0]);
    minY = Math.min(minY, vs[i][1]);
    maxY = Math.max(maxY, vs[i][1]);
  }
  return [(minX + maxX) / 2, (minY + maxY) / 2];
}

function getPolygonBounds(vs: [number, number][]) {
  if (vs.length === 0) return { minX: 0, maxX: 0, minY: 0, maxY: 0 };
  let minX = vs[0][0], maxX = vs[0][0], minY = vs[0][1], maxY = vs[0][1];
  for (let i = 1; i < vs.length; i++) {
    minX = Math.min(minX, vs[i][0]);
    maxX = Math.max(maxX, vs[i][0]);
    minY = Math.min(minY, vs[i][1]);
    maxY = Math.max(maxY, vs[i][1]);
  }
  return { minX, maxX, minY, maxY };
}

export default function Gallery() {
  const [project, setProject] = useState<Project>(DEFAULT_PROJECT);
  const [drawingShape, setDrawingShape] = useState<[number, number][]>([]);
  const [editorState, setEditorState] = useState<'IDLE' | 'DRAWING'>('IDLE');
  const [isGeneratingDepth, setIsGeneratingDepth] = useState(false);
  const [selectedShapeId, setSelectedShapeId] = useState<string | null>(null);
  const [previewCursor, setPreviewCursor] = useState<{ x: number, y: number, snapX: boolean, snapY: boolean } | null>(null);
  const [tick, setTick] = useState(0); // For forcing re-renders on image load
  const [resizeCount, setResizeCount] = useState(0);
  const [showWallpapersModal, setShowWallpapersModal] = useState(false);
  const [savedWallpapers, setSavedWallpapers] = useState<string[]>([]);

  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);

  // Cache for loaded images
  const imageCache = useRef<Record<string, HTMLImageElement>>({});

  // Interaction state
  const dragRef = useRef<{
    active: boolean;
    mode: 'pan' | 'vertex';
    startX?: number;
    startY?: number;
    initialPanX?: number;
    initialPanY?: number;
    shapeId?: string;
    vertexIdx?: number;
  } | null>(null);

  // History State
  const history = useRef<Project[]>([]);
  const historyIndex = useRef<number>(-1);

  const commitHistory = (p: Project) => {
    // Trim any redo history
    history.current = history.current.slice(0, historyIndex.current + 1);
    history.current.push(JSON.parse(JSON.stringify(p)));
    historyIndex.current++;
  };

  const handleUndo = () => {
    if (historyIndex.current > 0) {
      historyIndex.current--;
      setProject(history.current[historyIndex.current]);
    }
  };

  const handleRedo = () => {
    if (historyIndex.current < history.current.length - 1) {
      historyIndex.current++;
      setProject(history.current[historyIndex.current]);
    }
  };

  const handleClearAll = () => {
    if (window.confirm("Are you sure you want to clear the entire canvas?")) {
      const emptyProject = JSON.parse(JSON.stringify(DEFAULT_PROJECT));
      setProject(emptyProject);
      commitHistory(emptyProject);
      setSelectedShapeId(null);
    }
  };

  // Keyboard Shortcuts for Undo/Redo
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.ctrlKey && !e.shiftKey && e.key.toLowerCase() === 'z') {
        handleUndo();
      } else if ((e.ctrlKey && e.shiftKey && e.key.toLowerCase() === 'z') || (e.ctrlKey && e.key.toLowerCase() === 'y')) {
        handleRedo();
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, []);

  // Load project on mount
  useEffect(() => {
    const loadProject = async () => {
      try {
        const store = await load('gallery-project.json');
        const proj = await store.get<Project>('project');
        if (proj) {
          setProject(proj);
          commitHistory(proj);
        } else {
          commitHistory(DEFAULT_PROJECT);
        }
      } catch (err) {
        console.error("Failed to load project:", err);
      }
    };
    loadProject();

    const handleResize = () => setResizeCount(c => c + 1);
    window.addEventListener('resize', handleResize);
    return () => window.removeEventListener('resize', handleResize);
  }, []);

  // Wallpapers Fetcher
  useEffect(() => {
    if (showWallpapersModal) {
      listWallpapers().then(setSavedWallpapers).catch(console.error);
    }
  }, [showWallpapersModal]);

  // Render Pass
  useEffect(() => {
    const render = () => {
      const canvas = canvasRef.current;
      const container = containerRef.current;
      if (!canvas || !container) return;

      const ctx = canvas.getContext('2d');
      if (!ctx) return;

      // Match canvas resolution to container size (assuming 16:9 for now, or just filling container)
      const rect = container.getBoundingClientRect();
      const targetAspect = 16 / 9;

      let cw, ch;
      if (rect.width / rect.height > targetAspect) {
        ch = rect.height;
        cw = ch * targetAspect;
      } else {
        cw = rect.width;
        ch = cw / targetAspect;
      }

      // Handle high DPI displays
      const dpr = window.devicePixelRatio || 1;
      canvas.width = cw * dpr;
      canvas.height = ch * dpr;
      canvas.style.width = `${cw}px`;
      canvas.style.height = `${ch}px`;

      ctx.scale(dpr, dpr);

      // Helper to convert normalized to pixel coordinates
      const toPx = (nx: number, ny: number): [number, number] => [nx * cw, ny * ch];

      // 1. Draw Background
      if (project.background.type === 'color') {
        ctx.fillStyle = project.background.value;
        ctx.fillRect(0, 0, cw, ch);
      } else if (project.background.type === 'image' && project.background.value) {
        const img = getImage(project.background.value);
        if (img && img.complete) {
          // Implement fit modes
          if (project.background.fit === 'cover' || project.background.fit === 'contain') {
            const imgAspect = img.width / img.height;
            let drawW = cw, drawH = ch;
            if (project.background.fit === 'cover') {
              if (cw / ch > imgAspect) drawH = cw / imgAspect;
              else drawW = ch * imgAspect;
            } else {
              if (cw / ch > imgAspect) drawW = ch * imgAspect;
              else drawH = cw / imgAspect;
            }
            const dx = (cw - drawW) / 2;
            const dy = (ch - drawH) / 2;
            ctx.drawImage(img, dx, dy, drawW, drawH);
          } else if (project.background.fit === 'stretch') {
            ctx.drawImage(img, 0, 0, cw, ch);
          } else if (project.background.fit === 'tile') {
            const pat = ctx.createPattern(img, 'repeat');
            if (pat) {
              ctx.fillStyle = pat;
              ctx.fillRect(0, 0, cw, ch);
            }
          }
        }
      }

      // 2. Draw Completed Shapes
      project.shapes.forEach((shape) => {
        if (shape.vertices.length < 3) return;

        ctx.save();

        // Build Path
        ctx.beginPath();
        const start = toPx(shape.vertices[0][0], shape.vertices[0][1]);
        ctx.moveTo(start[0], start[1]);
        for (let i = 1; i < shape.vertices.length; i++) {
          const pt = toPx(shape.vertices[i][0], shape.vertices[i][1]);
          ctx.lineTo(pt[0], pt[1]);
        }
        ctx.closePath();

        // Clip to shape
        ctx.clip();

        // Draw Image inside clip
        if (shape.imagePath) {
          const img = getImage(shape.imagePath);
          if (img && img.complete) {
            const center = getPolygonCenter(shape.vertices);
            const cPx = toPx(center[0], center[1]);

            ctx.translate(cPx[0] + shape.transform.panX * cw, cPx[1] + shape.transform.panY * ch);
            ctx.rotate(shape.transform.rotate);
            ctx.scale(shape.transform.zoom, shape.transform.zoom);

            // Scale image to roughly match polygon bounds so it's not massive
            const bounds = getPolygonBounds(shape.vertices);
            const shapeW = (bounds.maxX - bounds.minX) * cw;
            const shapeH = (bounds.maxY - bounds.minY) * ch;
            const scaleX = shapeW / img.naturalWidth;
            const scaleY = shapeH / img.naturalHeight;
            const defaultScale = Math.max(scaleX, scaleY) || 1;

            const scaledW = img.naturalWidth * defaultScale;
            const scaledH = img.naturalHeight * defaultScale;

            ctx.drawImage(img, -scaledW / 2, -scaledH / 2, scaledW, scaledH);
          }
        } else {
          // Placeholder fill if no image
          ctx.fillStyle = shape.id === selectedShapeId ? 'rgba(0, 240, 255, 0.2)' : 'rgba(255, 255, 255, 0.1)';
          ctx.fill();
        }

        ctx.restore();

        // Draw Stroke (after restore to not be clipped)
        ctx.beginPath();
        ctx.moveTo(start[0], start[1]);
        for (let i = 1; i < shape.vertices.length; i++) {
          const pt = toPx(shape.vertices[i][0], shape.vertices[i][1]);
          ctx.lineTo(pt[0], pt[1]);
        }
        ctx.closePath();
        ctx.lineWidth = shape.id === selectedShapeId ? 3 : 1;
        ctx.strokeStyle = shape.id === selectedShapeId ? '#00F0FF' : 'rgba(255, 255, 255, 0.5)';
        ctx.stroke();

        // Draw vertices for selected shape to allow dragging
        if (shape.id === selectedShapeId && editorState === 'IDLE') {
          shape.vertices.forEach((v) => {
            const [vx, vy] = toPx(v[0], v[1]);
            ctx.beginPath();
            ctx.arc(vx, vy, 5, 0, Math.PI * 2);
            ctx.fillStyle = 'rgba(255, 255, 255, 0.8)';
            ctx.fill();
            ctx.strokeStyle = '#00F0FF';
            ctx.lineWidth = 2;
            ctx.stroke();
          });
        }
      });

      // 3. Draw Snapping Indicators
      if (previewCursor && (previewCursor.snapX || previewCursor.snapY) && (editorState === 'DRAWING' || dragRef.current?.active)) {
        ctx.save();
        ctx.strokeStyle = 'rgba(0, 240, 255, 0.8)';
        ctx.lineWidth = 2;
        ctx.beginPath();
        const px = previewCursor.x * cw;
        const py = previewCursor.y * ch;
        if (previewCursor.snapX) {
          ctx.moveTo(px, 0);
          ctx.lineTo(px, ch);
        }
        if (previewCursor.snapY) {
          ctx.moveTo(0, py);
          ctx.lineTo(cw, py);
        }
        ctx.stroke();

        ctx.fillStyle = 'rgba(0, 240, 255, 1)';
        ctx.beginPath();
        ctx.arc(px, py, 5, 0, Math.PI * 2);
        ctx.fill();
        ctx.restore();
      }

      // 4. Draw Active Drawing Shape
      if (editorState === 'DRAWING' && drawingShape.length > 0) {
        ctx.beginPath();
        const start = toPx(drawingShape[0][0], drawingShape[0][1]);
        ctx.moveTo(start[0], start[1]);
        for (let i = 1; i < drawingShape.length; i++) {
          const pt = toPx(drawingShape[i][0], drawingShape[i][1]);
          ctx.lineTo(pt[0], pt[1]);

          // Draw vertex points
          ctx.fillStyle = '#00F0FF';
          ctx.fillRect(pt[0] - 4, pt[1] - 4, 8, 8);
        }
        // Draw line to preview cursor
        if (previewCursor) {
          const pt = toPx(previewCursor.x, previewCursor.y);
          ctx.lineTo(pt[0], pt[1]);
        }
        ctx.fillStyle = '#00F0FF';
        ctx.fillRect(start[0] - 4, start[1] - 4, 8, 8);

        ctx.lineWidth = 2;
        ctx.strokeStyle = '#00F0FF';
        ctx.setLineDash([5, 5]);
        ctx.stroke();
        ctx.setLineDash([]);
      }

      // Removed animationFrameId = requestAnimationFrame(render);
    };

    render();

    // Cleanup not needed for single render pass
  }, [project, drawingShape, editorState, selectedShapeId, tick, resizeCount, previewCursor]);

  // Image Loader Helper
  const getImage = (path: string) => {
    if (imageCache.current[path]) {
      return imageCache.current[path];
    }
    const img = new Image();
    img.crossOrigin = "anonymous";
    img.onload = () => {
      setTick(t => t + 1);
    };
    img.onerror = (e) => {
      console.error("Failed to load image via convertFileSrc:", path, e);
    };
    // Convert absolute/relative path to a custom protocol URL that Tauri can load
    img.src = convertFileSrc(path);
    imageCache.current[path] = img;
    return img;
  };

  // Canvas Interactions
  const getNormalizedMousePos = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    if (!canvas) return [0, 0];
    const rect = canvas.getBoundingClientRect();
    const x = (e.clientX - rect.left) / rect.width;
    const y = (e.clientY - rect.top) / rect.height;
    return [x, y] as [number, number];
  };

  const SNAP_THRESHOLD_PX = 15;
  const getSnappedNormalizedMousePos = (e: React.MouseEvent<HTMLCanvasElement>): [number, number, boolean, boolean] => {
    const canvas = canvasRef.current;
    if (!canvas) return [0, 0, false, false];
    const rect = canvas.getBoundingClientRect();
    let x = (e.clientX - rect.left);
    let y = (e.clientY - rect.top);

    let snapX = false;
    let snapY = false;

    if (x < SNAP_THRESHOLD_PX) { x = 0; snapX = true; }
    else if (x > rect.width - SNAP_THRESHOLD_PX) { x = rect.width; snapX = true; }

    if (y < SNAP_THRESHOLD_PX) { y = 0; snapY = true; }
    else if (y > rect.height - SNAP_THRESHOLD_PX) { y = rect.height; snapY = true; }

    return [x / rect.width, y / rect.height, snapX, snapY];
  };

  const handlePointerDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const [nx, ny] = getSnappedNormalizedMousePos(e);

    if (editorState === 'DRAWING') {
      // If clicking near the start vertex, close the shape
      if (drawingShape.length >= 3) {
        const startX = drawingShape[0][0];
        const startY = drawingShape[0][1];
        const dist = Math.sqrt((nx - startX) ** 2 + (ny - startY) ** 2);
        // Arbitrary small distance threshold for closing (normalized)
        if (dist < 0.03) {
          const newShape: Shape = {
            id: `shape-${Date.now()}`,
            vertices: drawingShape,
            imagePath: null,
            transform: { panX: 0, panY: 0, zoom: 1, rotate: 0 }
          };
          setProject(prev => {
            const next = { ...prev, shapes: [...prev.shapes, newShape] };
            commitHistory(next);
            return next;
          });
          setSelectedShapeId(newShape.id);
          setDrawingShape([]);
          setEditorState('IDLE');
          return;
        }
      }
      setDrawingShape([...drawingShape, [nx, ny]]);
      return;
    }

    // Check for vertex dragging on selected shape
    if (editorState === 'IDLE' && selectedShapeId) {
      const shape = project.shapes.find(s => s.id === selectedShapeId);
      if (shape && canvasRef.current) {
        const rect = canvasRef.current.getBoundingClientRect();
        const hitIdx = shape.vertices.findIndex(v => {
          const vxPx = v[0] * rect.width;
          const vyPx = v[1] * rect.height;
          const mxPx = (e.clientX - rect.left);
          const myPx = (e.clientY - rect.top);
          return Math.sqrt((vxPx - mxPx) ** 2 + (vyPx - myPx) ** 2) < 10; // 10px grab radius
        });

        if (hitIdx !== -1) {
          dragRef.current = {
            active: true,
            mode: 'vertex',
            shapeId: selectedShapeId,
            vertexIdx: hitIdx
          };
          return;
        }
      }
    }

    // Select Shape (iterate backwards to select top-most first)
    let clickedShapeId: string | null = null;
    const [rawNx, rawNy] = getNormalizedMousePos(e);
    for (let i = project.shapes.length - 1; i >= 0; i--) {
      if (isPointInPolygon([rawNx, rawNy], project.shapes[i].vertices)) {
        clickedShapeId = project.shapes[i].id;
        break;
      }
    }

    setSelectedShapeId(clickedShapeId);

    // Initiate pan if a shape with an image is clicked
    if (clickedShapeId) {
      const shape = project.shapes.find(s => s.id === clickedShapeId);
      if (shape && shape.imagePath) {
        dragRef.current = {
          active: true,
          mode: 'pan',
          startX: rawNx,
          startY: rawNy,
          initialPanX: shape.transform.panX,
          initialPanY: shape.transform.panY
        };
      }
    }
  };

  const handlePointerMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const [nx, ny, snapX, snapY] = getSnappedNormalizedMousePos(e);

    setPreviewCursor({ x: nx, y: ny, snapX, snapY });

    if (!dragRef.current?.active) return;

    if (dragRef.current.mode === 'vertex' && dragRef.current.shapeId) {
      setProject(prev => ({
        ...prev,
        shapes: prev.shapes.map(s => {
          if (s.id === dragRef.current!.shapeId) {
            const newVertices = [...s.vertices];
            newVertices[dragRef.current!.vertexIdx!] = [nx, ny];
            return { ...s, vertices: newVertices };
          }
          return s;
        })
      }));
      return;
    }

    if (dragRef.current.mode === 'pan' && selectedShapeId) {
      const [rawNx, rawNy] = getNormalizedMousePos(e);
      const dx = rawNx - dragRef.current.startX!;
      const dy = rawNy - dragRef.current.startY!;

      setProject(prev => ({
        ...prev,
        shapes: prev.shapes.map(s => {
          if (s.id === selectedShapeId) {
            return {
              ...s,
              transform: {
                ...s.transform,
                panX: dragRef.current!.initialPanX! + dx,
                panY: dragRef.current!.initialPanY! + dy
              }
            };
          }
          return s;
        })
      }));
    }
  };

  const handlePointerUp = () => {
    if (dragRef.current?.active) {
      dragRef.current.active = false;
      setProject(prev => {
        commitHistory(prev);
        return prev;
      });
    }
    setPreviewCursor(null);
  };

  const handlePointerLeave = () => {
    if (dragRef.current) {
      dragRef.current.active = false;
    }
    setPreviewCursor(null);
  };

  const handleWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    if (!selectedShapeId) return;
    const shape = project.shapes.find(s => s.id === selectedShapeId);
    if (!shape || !shape.imagePath) return;

    const zoomDelta = e.deltaY * -0.001;
    updateShapeTransform(selectedShapeId, { zoom: Math.max(0.1, shape.transform.zoom + zoomDelta) });
  };

  const updateShapeTransform = (id: string, partial: Partial<Transform>) => {
    setProject(prev => ({
      ...prev,
      shapes: prev.shapes.map(s =>
        s.id === id ? { ...s, transform: { ...s.transform, ...partial } } : s
      )
    }));
  };

  // Actions
  const startDrawing = () => {
    setEditorState('DRAWING');
    setSelectedShapeId(null);
    setDrawingShape([]);
  };

  const cancelDrawing = () => {
    setEditorState('IDLE');
    setDrawingShape([]);
  };

  const deleteShape = (id: string) => {
    setProject(prev => {
      const next = { ...prev, shapes: prev.shapes.filter(s => s.id !== id) };
      commitHistory(next);
      return next;
    });
    if (selectedShapeId === id) setSelectedShapeId(null);
  };

  const handleImportImageForShape = async (id: string) => {
    const selected = await open({
      multiple: false,
      filters: [{ name: 'Image', extensions: ['png', 'jpeg', 'jpg', 'webp', 'bmp'] }]
    });

    if (selected && typeof selected === 'string') {
      try {
        const newPath = await importWallpaper(selected);
        if (newPath) {
          setProject(prev => {
            const next = {
              ...prev,
              shapes: prev.shapes.map(s =>
                s.id === id ? { ...s, imagePath: newPath } : s
              )
            };
            commitHistory(next);
            return next;
          });
        }
      } catch (err) {
        console.error("Failed to import image:", err);
      }
    }
  };

  const handleImportBackground = async () => {
    const selected = await open({
      multiple: false,
      filters: [{ name: 'Image', extensions: ['png', 'jpeg', 'jpg', 'webp', 'bmp'] }]
    });

    if (selected && typeof selected === 'string') {
      try {
        const newPath = await importWallpaper(selected);
        if (newPath) {
          setProject(prev => {
            const next = {
              ...prev,
              background: { ...prev.background, type: 'image' as const, value: newPath }
            };
            commitHistory(next);
            return next;
          });
        }
      } catch (err) {
        console.error("Failed to import background image:", err);
      }
    }
  };

  const saveProject = async (silent = false) => {
    try {
      const store = await load('gallery-project.json');
      await store.set('project', project);
      await store.save();
      if (!silent) alert("Project saved successfully!");
    } catch (err) {
      console.error("Failed to save project:", err);
      if (!silent) alert("Failed to save project.");
    }
  };

  const bakeCanvas = async (): Promise<Uint8Array> => {
    // 1. Calculate Target Resolution
    let maxSourceWidth = 0;

    if (project.background.type === 'image' && project.background.value) {
      const bgImg = imageCache.current[project.background.value];
      if (bgImg) maxSourceWidth = Math.max(maxSourceWidth, bgImg.width);
    }

    project.shapes.forEach(shape => {
      if (shape.imagePath) {
        const img = imageCache.current[shape.imagePath];
        if (img) maxSourceWidth = Math.max(maxSourceWidth, img.width);
      }
    });

    // Default to 1920 if no images exist
    if (maxSourceWidth === 0) maxSourceWidth = 1920;

    // Cap at 2560 (2K), minimum 1920, and respect screen width if it's smaller
    const targetWidth = Math.min(2560, Math.max(1920, maxSourceWidth), window.screen.width * (window.devicePixelRatio || 1));
    const targetHeight = targetWidth / (16 / 9); // Assuming 16:9 for target wallpaper aspect

    // 2. Offscreen Canvas Render
    const offCanvas = document.createElement('canvas');
    offCanvas.width = targetWidth;
    offCanvas.height = targetHeight;
    const ctx = offCanvas.getContext('2d');
    if (!ctx) throw new Error("Failed to get context");

    const cw = targetWidth;
    const ch = targetHeight;
    const toPx = (nx: number, ny: number): [number, number] => [nx * cw, ny * ch];

    // Background
    if (project.background.type === 'color') {
      ctx.fillStyle = project.background.value;
      ctx.fillRect(0, 0, cw, ch);
    } else if (project.background.type === 'image' && project.background.value) {
      const img = imageCache.current[project.background.value];
      if (img && img.complete) {
        if (project.background.fit === 'cover' || project.background.fit === 'contain') {
          const imgAspect = img.width / img.height;
          let drawW = cw, drawH = ch;
          if (project.background.fit === 'cover') {
            if (cw / ch > imgAspect) drawH = cw / imgAspect;
            else drawW = ch * imgAspect;
          } else {
            if (cw / ch > imgAspect) drawW = ch * imgAspect;
            else drawH = cw / imgAspect;
          }
          const dx = (cw - drawW) / 2;
          const dy = (ch - drawH) / 2;
          ctx.drawImage(img, dx, dy, drawW, drawH);
        } else if (project.background.fit === 'stretch') {
          ctx.drawImage(img, 0, 0, cw, ch);
        } else if (project.background.fit === 'tile') {
          const pat = ctx.createPattern(img, 'repeat');
          if (pat) {
            ctx.fillStyle = pat;
            ctx.fillRect(0, 0, cw, ch);
          }
        }
      }
    }

    // Shapes
    project.shapes.forEach((shape) => {
      if (shape.vertices.length < 3) return;
      ctx.save();
      ctx.beginPath();
      const start = toPx(shape.vertices[0][0], shape.vertices[0][1]);
      ctx.moveTo(start[0], start[1]);
      for (let i = 1; i < shape.vertices.length; i++) {
        const pt = toPx(shape.vertices[i][0], shape.vertices[i][1]);
        ctx.lineTo(pt[0], pt[1]);
      }
      ctx.closePath();
      ctx.clip();

      if (shape.imagePath) {
        const img = imageCache.current[shape.imagePath];
        if (img && img.complete) {
          const center = getPolygonCenter(shape.vertices);
          const cPx = toPx(center[0], center[1]);

          ctx.translate(cPx[0] + shape.transform.panX * cw, cPx[1] + shape.transform.panY * ch);
          ctx.rotate(shape.transform.rotate);
          ctx.scale(shape.transform.zoom, shape.transform.zoom);

          const bounds = getPolygonBounds(shape.vertices);
          const shapeW = (bounds.maxX - bounds.minX) * cw;
          const shapeH = (bounds.maxY - bounds.minY) * ch;
          const scaleX = shapeW / img.naturalWidth;
          const scaleY = shapeH / img.naturalHeight;
          const defaultScale = Math.max(scaleX, scaleY) || 1;

          const scaledW = img.naturalWidth * defaultScale;
          const scaledH = img.naturalHeight * defaultScale;

          ctx.drawImage(img, -scaledW / 2, -scaledH / 2, scaledW, scaledH);
        }
      } else {
        ctx.fillStyle = 'rgba(255, 255, 255, 0.1)';
        ctx.fill();
      }

      ctx.restore();
    });

    // 3. Bake and Save
    const blob = await new Promise<Blob | null>(res => offCanvas.toBlob(res, "image/png"));
    if (!blob) throw new Error("Failed to bake image");

    const arrayBuffer = await blob.arrayBuffer();
    return new Uint8Array(arrayBuffer);
  };

  const handleActivate = async () => {
    try {
      setIsGeneratingDepth(true);
      const bytes = await bakeCanvas();
      const savedPath = await saveBakedWallpaper(`active-collage.png`, bytes);

      // 4. Apply
      await saveProject(true); // update the gallery-project.json silently
      await saveActiveSession({ layerA: savedPath, layerB: "", effect: "none", isGalleryCollage: true });
      await removeEffect();
      await applyWallpaper(savedPath, ""); // set as active wallpaper
      setIsGeneratingDepth(false);
      alert("Gallery wallpaper activated successfully!");
    } catch (err: any) {
      setIsGeneratingDepth(false);
      console.error("Failed to activate gallery wallpaper:", err);
      alert(`Failed to activate: ${err.toString()}`);
    }
  };

  const handleSaveBake = async () => {
    try {
      setIsGeneratingDepth(true);
      const bytes = await bakeCanvas();
      const hashBuffer = await crypto.subtle.digest('SHA-256', bytes);
      const hashArray = Array.from(new Uint8Array(hashBuffer));
      const hashHex = hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
      await saveBakedWallpaper(`gallery-${hashHex}.png`, bytes);

      await saveProject(false);
      setIsGeneratingDepth(false);
    } catch (err: any) {
      setIsGeneratingDepth(false);
      console.error("Failed to save and bake gallery wallpaper:", err);
      alert(`Failed to save: ${err.toString()}`);
    }
  };

  const selectedShape = project.shapes.find(s => s.id === selectedShapeId);

  return (
    <div className="gallery-container">
      <div className="gallery-canvas-area" ref={containerRef}>
        <canvas
          ref={canvasRef}
          className={`gallery-canvas ${editorState === 'DRAWING' ? 'drawing' : 'idle'}`}
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
          onPointerLeave={handlePointerLeave}
          onWheel={handleWheel}
        />
        <div className="gallery-action-bar">
          {editorState === 'DRAWING' ? (
            <>
              <span style={{ color: 'white', padding: '0 10px', display: 'flex', alignItems: 'center' }}>Click to add vertices. Connect to start to finish.</span>
              <button className="danger" onClick={cancelDrawing} style={{ padding: '4px 10px' }}><X size={16} /></button>
            </>
          ) : (
            <>
              <button className="secondary" onClick={handleUndo} title="Undo (Ctrl+Z)">
                <Undo2 size={16} />
              </button>
              <button className="secondary" onClick={handleRedo} title="Redo (Ctrl+Y)">
                <Redo2 size={16} />
              </button>
              <button className="danger" onClick={handleClearAll} title="Clear All Canvas">
                <Trash2 size={16} />
              </button>
              <div style={{ width: '1px', background: 'rgba(255,255,255,0.2)', margin: '0 5px' }}></div>
              <button className="primary" onClick={handleActivate}>
                <Check size={16} /> Activate
              </button>
              <button className="secondary" onClick={handleSaveBake}>
                <Save size={16} /> Save
              </button>
              <button className="secondary" onClick={startDrawing}>
                <Plus size={16} /> Add Shape
              </button>
              <div style={{ width: '1px', background: 'rgba(255,255,255,0.2)', margin: '0 5px' }}></div>
              <button className="secondary" onClick={() => setShowWallpapersModal(true)}>
                <FolderOpen size={16} /> My Wallpapers
              </button>
              <div style={{ width: '1px', background: 'rgba(255,255,255,0.2)', margin: '0 5px' }}></div>
              <button className="danger" onClick={async () => {
                await clearWallpaper();
                await saveActiveSession(null);
                alert("Wallpaper stopped and reset.");
              }}>
                Stop Wallpaper
              </button>
            </>
          )}
        </div>
        {isGeneratingDepth && (
          <div style={{
            position: 'absolute', top: 0, left: 0, right: 0, bottom: 0,
            background: 'rgba(0,0,0,0.7)', display: 'flex', alignItems: 'center', justifyContent: 'center', zIndex: 100,
            flexDirection: 'column', gap: '1rem', backdropFilter: 'blur(4px)', borderRadius: '8px'
          }}>
            <div className="spinner"></div>
            <div style={{ color: 'var(--text-secondary)' }}>Baking Wallpaper...</div>
          </div>
        )}
      </div>
      <div className="gallery-inspector">
        <div className="card" style={{ flexShrink: 0 }}>
          <h2>Background</h2>
          <div className="control-group">
            <label>Type</label>
            <select
              value={project.background.type}
              onChange={e => setProject(p => { const next = { ...p, background: { ...p.background, type: e.target.value as 'color' | 'image' } }; commitHistory(next); return next; })}
            >
              <option value="color">Solid Color</option>
              <option value="image">Image</option>
            </select>
          </div>

          {project.background.type === 'color' ? (
            <div className="control-group">
              <label>Color</label>
              <input
                type="color"
                value={project.background.value}
                onChange={e => setProject(p => { const next = { ...p, background: { ...p.background, value: e.target.value } }; commitHistory(next); return next; })}
                style={{ width: '100%', height: '40px', background: 'transparent', border: '1px solid var(--border-color)', borderRadius: '4px', cursor: 'pointer' }}
              />
            </div>
          ) : (
            <>
              <button className="secondary" onClick={handleImportBackground} style={{ width: '100%', display: 'flex', justifyContent: 'center', gap: '8px', marginBottom: '15px' }}>
                <Upload size={18} /> Import Background Image
              </button>
              <div className="control-group">
                <label>Fit Mode</label>
                <select
                  value={project.background.fit}
                  onChange={e => setProject(p => { const next = { ...p, background: { ...p.background, fit: e.target.value as Background['fit'] } }; commitHistory(next); return next; })}
                >
                  <option value="cover">Cover</option>
                  <option value="contain">Contain</option>
                  <option value="stretch">Stretch</option>
                  <option value="tile">Tile</option>
                </select>
              </div>
            </>
          )}
        </div>

        <div className="card" style={{ flexShrink: 0 }}>
          <h2>Shapes List</h2>
          {project.shapes.length === 0 ? (
            <p style={{ color: 'var(--text-secondary)', fontSize: '0.9rem' }}>No shapes added yet.</p>
          ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: '8px', marginTop: '10px' }}>
              {project.shapes.map((shape, index) => (
                <div
                  key={shape.id}
                  onClick={() => setSelectedShapeId(shape.id)}
                  style={{
                    padding: '8px 12px',
                    backgroundColor: selectedShapeId === shape.id ? 'var(--accent)' : 'rgba(255, 255, 255, 0.05)',
                    color: selectedShapeId === shape.id ? '#000' : 'white',
                    borderRadius: '6px',
                    cursor: 'pointer',
                    display: 'flex',
                    justifyContent: 'space-between',
                    alignItems: 'center',
                    fontWeight: selectedShapeId === shape.id ? 'bold' : 'normal'
                  }}
                >
                  <span>Shape {index + 1}</span>
                  {selectedShapeId === shape.id && <Check size={16} />}
                </div>
              ))}
            </div>
          )}
        </div>

        {selectedShape ? (
          <div className="card" style={{ flexShrink: 0 }}>
            <h2 style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              Shape Details
              <button className="danger" onClick={() => deleteShape(selectedShape.id)} style={{ padding: '4px 8px', fontSize: '0.8rem' }}>Delete</button>
            </h2>

            <button className="secondary" onClick={() => handleImportImageForShape(selectedShape.id)} style={{ width: '100%', display: 'flex', justifyContent: 'center', gap: '8px', margin: '15px 0' }}>
              <ImageIcon size={18} /> {selectedShape.imagePath ? 'Change Image' : 'Import Image'}
            </button>

            {selectedShape.imagePath && (
              <>
                <p style={{ fontSize: '0.8rem', color: 'var(--text-secondary)', marginBottom: '20px' }}>
                  Tip: Drag the shape in the canvas to pan the image. Use mouse wheel to zoom.
                </p>

                <div className="control-group">
                  <label>Zoom ({selectedShape.transform.zoom.toFixed(2)}x)</label>
                  <input
                    type="range" min="0.1" max="5.0" step="0.001"
                    value={selectedShape.transform.zoom}
                    onChange={e => updateShapeTransform(selectedShape.id, { zoom: parseFloat(e.target.value) })}
                  />
                </div>

                <div className="control-group">
                  <label>Rotation ({(selectedShape.transform.rotate * (180 / Math.PI)).toFixed(0)}°)</label>
                  <input
                    type="range" min="-3.14159" max="3.14159" step="0.001"
                    value={selectedShape.transform.rotate}
                    onChange={e => updateShapeTransform(selectedShape.id, { rotate: parseFloat(e.target.value) })}
                  />
                </div>
              </>
            )}
          </div>
        ) : (
          <div className="card" style={{ flexShrink: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', color: 'var(--text-secondary)', textAlign: 'center', minHeight: '100px' }}>
            Select a shape on the canvas or from the list to edit its properties.
          </div>
        )}
      </div>

      {showWallpapersModal && (
        <div style={{
          position: 'fixed', top: 0, left: 0, right: 0, bottom: 0,
          background: 'rgba(0,0,0,0.8)', backdropFilter: 'blur(10px)',
          display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center',
          zIndex: 100
        }}>
          <div className="card" style={{ width: '80%', height: '80%', display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid var(--border-color)', paddingBottom: '1rem', marginBottom: '1rem' }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem' }}>
                <h2 style={{margin: 0}}>My Baked Wallpapers</h2>
                <div title="%APPDATA%\Graffiti\baked_wallpapers" style={{cursor: 'help', display: 'flex', color: 'var(--text-secondary)'}}>
                  <Info size={16} />
                </div>
              </div>
              <button className="secondary" onClick={() => setShowWallpapersModal(false)}><X size={20} /></button>
            </div>
            <div className="thumbnail-grid" style={{ overflowY: 'auto', flex: 1 }}>
              {savedWallpapers.length === 0 ? (
                <p style={{ color: 'rgba(255,255,255,0.5)', gridColumn: '1 / -1', textAlign: 'center', marginTop: '2rem' }}>No baked wallpapers found.</p>
              ) : (
                savedWallpapers.map((path, idx) => (
                  <div key={idx} className="wallpaper-card" style={{ position: 'relative' }}>
                    <img src={convertFileSrc(path)} alt={`Wallpaper ${idx}`} />

                    <button className="secondary" style={{ position: 'absolute', top: '8px', right: '8px', padding: '6px', background: 'rgba(0,0,0,0.6)', border: 'none', borderRadius: '4px' }}
                      onClick={async () => {
                        if (confirm("Are you sure you want to delete this wallpaper?")) {
                          try {
                            await deleteBakedWallpaper(path);
                            const wps = await listWallpapers();
                            setSavedWallpapers(wps);
                          } catch (err: any) {
                            console.error("Failed to delete wallpaper:", err);
                            alert(`Failed to delete wallpaper: ${err.toString()}`);
                          }
                        }
                      }}
                    >
                      <Trash2 size={16} color="white" />
                    </button>

                    <button className="primary" style={{ width: '100%', padding: '8px', marginTop: '8px' }} onClick={async () => {
                      try {
                        await saveActiveSession({ layerA: path, layerB: "", effect: "none", isGalleryCollage: true });
                        await removeEffect();
                        await applyWallpaper(path, "");
                        alert("Wallpaper activated successfully!");
                        setShowWallpapersModal(false);
                      } catch (err: any) {
                        alert(`Failed to activate wallpaper: ${err.toString()}`);
                      }
                    }}>
                      Apply
                    </button>
                  </div>
                ))
              )}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
