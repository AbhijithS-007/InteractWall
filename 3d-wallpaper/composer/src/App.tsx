import React, { useState, useEffect, Component, Suspense } from 'react'
import type { ReactNode } from 'react'
import { Canvas, useThree } from '@react-three/fiber'
import { Grid, OrbitControls, useGLTF, TransformControls, Environment } from '@react-three/drei'
import * as THREE from 'three'
import './App.css'

// Error boundary to catch useGLTF loading failures
class ModelErrorBoundary extends Component<
  { children: ReactNode; fallback: ReactNode },
  { hasError: boolean }
> {
  constructor(props: { children: ReactNode; fallback: ReactNode }) {
    super(props)
    this.state = { hasError: false }
  }
  static getDerivedStateFromError() {
    return { hasError: true }
  }
  render() {
    if (this.state.hasError) return this.props.fallback
    return this.props.children
  }
}

function ModelInner({ url }: { url: string }) {
  const { scene } = useGLTF(`/${url}`)
  
  // Replicate the C++ engine's center-and-normalize behavior
  const normalizedScene = React.useMemo(() => {
    const clone = scene.clone()
    
    // Reset transforms to measure raw size
    clone.position.set(0, 0, 0)
    clone.scale.set(1, 1, 1)
    clone.rotation.set(0, 0, 0)
    clone.updateMatrixWorld(true)

    const box = new THREE.Box3().setFromObject(clone)
    const center = box.getCenter(new THREE.Vector3())
    const size = box.getSize(new THREE.Vector3())

    // Shift model so its bounding box center is at origin
    clone.position.set(-center.x, -center.y, -center.z)
    
    // Scale so the max extent is exactly 2.0 (same as C++)
    const maxExtent = Math.max(size.x, size.y, size.z)
    const normalizeScale = maxExtent > 0 ? 2.0 / maxExtent : 1.0
    
    const group = new THREE.Group()
    group.scale.setScalar(normalizeScale)
    group.add(clone)
    
    return group
  }, [scene])

  return <primitive object={normalizedScene} />
}

function FallbackBox() {
  return (
    <mesh>
      <boxGeometry args={[1, 1, 1]} />
      <meshStandardMaterial color="hotpink" />
    </mesh>
  )
}

function CameraResetter({ trigger }: { trigger: number }) {
  const { camera, controls } = useThree() as any
  useEffect(() => {
    camera.position.set(0, 0, 4)
    camera.lookAt(0, 0, 0)
    if (controls) {
      controls.target.set(0, 0, 0)
      controls.update()
    }
  }, [trigger, camera, controls])
  return null
}

function SceneSettings({ exposure }: { exposure: number }) {
  const { gl } = useThree()
  useEffect(() => {
    gl.toneMappingExposure = exposure
  }, [gl, exposure])
  return null
}

function Model({ url }: { url: string }) {
  return (
    <ModelErrorBoundary fallback={<FallbackBox />}>
      <Suspense fallback={<FallbackBox />}>
        <ModelInner url={url} />
      </Suspense>
    </ModelErrorBoundary>
  )
}

function App() {
  const [sceneData, setSceneData] = useState<any>(null)
  const [status, setStatus] = useState('Loading...')
  
  // Model state bindings
  const [modelPath, setModelPath] = useState('models/Duck.glb')
  const [bgType, setBgType] = useState('image')
  const [bgValue, setBgValue] = useState('background.jpg')
  const [qualityTier, setQualityTier] = useState<'low' | 'balanced' | 'high'>('high')
  
  const [resetCameraTrigger, setResetCameraTrigger] = useState(0)

  // Transform state
  const [transformMode, setTransformMode] = useState<'translate' | 'rotate' | 'scale'>('translate')
  const [objPos, setObjPos] = useState<[number, number, number]>([0, 0, 0])
  const [objRot, setObjRot] = useState<[number, number, number]>([0, 0, 0]) // in degrees
  const [objScale, setObjScale] = useState<[number, number, number]>([1, 1, 1])
  
  // Camera / interaction state
  const [sensitivity, setSensitivity] = useState<number>(0.3)
  const [maxRotationOffset, setMaxRotationOffset] = useState<number>(30)
  const [rotationMultiplier, setRotationMultiplier] = useState<number>(1.0)
  
  // Light state
  const [lightPos, setLightPos] = useState<[number, number, number]>([10, 10, 5])
  const [exposure, setExposure] = useState<number>(1.0)
  
  const lightRef = React.useRef<THREE.Group>(null!)
  const modelRef = React.useRef<THREE.Group>(null!)

  useEffect(() => {
    fetch('/api/scene')
      .then(r => r.json())
      .then(data => {
        if (!data.error) {
          setSceneData(data)
          if (data.objects && data.objects.length > 0) {
            setModelPath(data.objects[0].modelPath)
            if (data.objects[0].position) setObjPos(data.objects[0].position)
            if (data.objects[0].rotation) setObjRot(data.objects[0].rotation)
            if (data.objects[0].scale) setObjScale(data.objects[0].scale)
            if (data.objects[0].rotationMultiplier !== undefined) setRotationMultiplier(data.objects[0].rotationMultiplier)
          }
          if (data.background) {
            setBgType(data.background.type || 'color')
            setBgValue(data.background.value || '#101418')
          }
          if (data.lighting && data.lighting.directionalLights && data.lighting.directionalLights.length > 0) {
            const dir = data.lighting.directionalLights[0].direction
            setLightPos([-dir[0] * 10, -dir[1] * 10, -dir[2] * 10])
          }
          if (data.lighting && data.lighting.exposure !== undefined) {
            setExposure(data.lighting.exposure)
          }
          if (data.camera) {
            if (data.camera.sensitivity !== undefined) setSensitivity(data.camera.sensitivity)
            if (data.camera.maxRotationOffset !== undefined) setMaxRotationOffset(data.camera.maxRotationOffset)
          }
          if (data.rendering && data.rendering.qualityTier !== undefined) {
            setQualityTier(data.rendering.qualityTier)
          }
        }
        setStatus('')
      })
      .catch(e => setStatus(`Error loading scene: ${e.message}`))
  }, [])

  const handleUpdate = async () => {
    setStatus('Saving...')
    // Deep clone existing sceneData so we don't mutate React state
    const newSceneData = sceneData ? JSON.parse(JSON.stringify(sceneData)) : {
      version: 1, name: "My Composed Scene", camera: { fov: 45, sensitivity: 0.3, maxRotationOffset: 30.0 }, rendering: { lightingEnabled: true }, lighting: { ambientColor: "#333333", directionalLights: [{ color: "#ffffff", direction: [-0.5, -1.0, 0.5], intensity: 1.0 }] }
    }

    if (!newSceneData.camera) newSceneData.camera = { fov: 45 }
    newSceneData.camera.sensitivity = sensitivity
    newSceneData.camera.maxRotationOffset = maxRotationOffset
    
    if (!newSceneData.rendering) newSceneData.rendering = { lightingEnabled: true }
    newSceneData.rendering.qualityTier = qualityTier

    newSceneData.background = { type: bgType, value: bgValue, fit: "cover" }
    
    if (!newSceneData.objects || newSceneData.objects.length === 0) {
      newSceneData.objects = [{ id: "obj_1", position: [0, 0, 0], rotation: [0, 0, 0], scale: [1, 1, 1], followMouse: true, rotationMultiplier: 1.0 }]
    }
    newSceneData.objects[0].modelPath = modelPath
    newSceneData.objects[0].position = objPos
    newSceneData.objects[0].rotation = objRot
    newSceneData.objects[0].scale = objScale
    newSceneData.objects[0].rotationMultiplier = rotationMultiplier
    
    if (!newSceneData.lighting) newSceneData.lighting = { ambientColor: "#333333", directionalLights: [] }
    if (!newSceneData.lighting.directionalLights || newSceneData.lighting.directionalLights.length === 0) {
      newSceneData.lighting.directionalLights = [{ color: "#ffffff", direction: [-0.5, -0.8, -0.5], intensity: 1.0 }]
    }
    const lightLen = Math.hypot(lightPos[0], lightPos[1], lightPos[2]) || 1;
    newSceneData.lighting.directionalLights[0].direction = [
      -lightPos[0] / lightLen,
      -lightPos[1] / lightLen,
      -lightPos[2] / lightLen
    ]
    newSceneData.lighting.exposure = exposure

    try {
      const res = await fetch('/api/save', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(newSceneData)
      })
      if (res.ok) {
        setSceneData(newSceneData)
        setStatus('Saved! C++ engine should hot-reload.')
        setTimeout(() => setStatus(''), 3000)
      } else {
        setStatus('Failed to save.')
      }
    } catch (e) {
      setStatus(`Error: ${(e as Error).message}`)
    }
  }

  const handleApplyWallpaper = async () => {
    // Save scene first
    await handleUpdate()
    setStatus('Applying as Wallpaper...')
    
    try {
      const res = await fetch('/api/apply-wallpaper', {
        method: 'POST',
      })
      if (res.ok) {
        setStatus('Wallpaper applied successfully!')
        setTimeout(() => setStatus(''), 3000)
      } else {
        const err = await res.json()
        setStatus(`Failed to apply wallpaper: ${err.error}`)
      }
    } catch (e) {
      setStatus(`Error: ${(e as Error).message}`)
    }
  }

  const handleReset = () => {
    setObjPos([0, 0, 0])
    setObjRot([0, 0, 0])
    setObjScale([1, 1, 1])
    setLightPos([10, 10, 5])
    setResetCameraTrigger(prev => prev + 1)
  }

  return (
    <div style={{ width: '100vw', height: '100vh', display: 'flex' }}>
      {/* 3D Viewport */}
      <div style={{ flex: 1, position: 'relative' }}>
        <Canvas camera={{ position: [0, 0, 4], fov: 45 }}>
          <SceneSettings exposure={exposure} />
          <CameraResetter trigger={resetCameraTrigger} />
          {bgType === 'color' ? (
            <color attach="background" args={[bgValue]} />
          ) : (
            <color attach="background" args={['#101418']} /> // Fallback for image
          )}
          <ambientLight intensity={1.0} />
          
          <Environment preset="city" />
          
          <group ref={lightRef} position={lightPos}>
            <mesh name="lightMesh">
              <sphereGeometry args={[0.3, 16, 16]} />
              <meshBasicMaterial color="#ffffaa" />
              <directionalLight intensity={1} />
            </mesh>
          </group>
          <TransformControls 
            object={lightRef}
            mode="translate"
            size={0.5}
            showX showY showZ
            onMouseUp={(e: any) => {
              if (lightRef.current) {
                const pos = lightRef.current.position
                setLightPos([
                  parseFloat(pos.x.toFixed(3)),
                  parseFloat(pos.y.toFixed(3)),
                  parseFloat(pos.z.toFixed(3))
                ])
              }
            }}
          />
          
          <Grid infiniteGrid fadeDistance={20} sectionColor="#444" cellColor="#222" />
          <axesHelper args={[5]} />
          
          {modelPath && (
            <>
              <group 
                ref={modelRef}
                position={objPos}
                rotation={objRot.map(THREE.MathUtils.degToRad) as any}
                scale={objScale}
                name="modelGroup"
              >
                <Model url={modelPath} />
              </group>
              <TransformControls 
                object={modelRef}
                mode={transformMode}
                onMouseUp={(e: any) => {
                  if (modelRef.current) {
                    const obj = modelRef.current
                    setObjPos([
                      parseFloat(obj.position.x.toFixed(3)), 
                      parseFloat(obj.position.y.toFixed(3)), 
                      parseFloat(obj.position.z.toFixed(3))
                    ])
                    
                    setObjRot([
                      parseFloat(THREE.MathUtils.radToDeg(obj.rotation.x).toFixed(3)),
                      parseFloat(THREE.MathUtils.radToDeg(obj.rotation.y).toFixed(3)),
                      parseFloat(THREE.MathUtils.radToDeg(obj.rotation.z).toFixed(3))
                    ])
                    
                    setObjScale([
                      parseFloat(obj.scale.x.toFixed(3)), 
                      parseFloat(obj.scale.y.toFixed(3)), 
                      parseFloat(obj.scale.z.toFixed(3))
                    ])
                  }
                }}
              />
            </>
          )}
          
          <OrbitControls makeDefault />
        </Canvas>
      </div>

      {/* UI Panel */}
      <div style={{ 
        width: '350px', 
        backgroundColor: '#1e1e1e', 
        color: '#fff', 
        padding: '20px', 
        boxSizing: 'border-box',
        borderLeft: '1px solid #333',
        display: 'flex',
        flexDirection: 'column',
        gap: '20px',
        overflowY: 'auto'
      }}>
        <h2>Scene Composer</h2>
        
        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <label>Transform Mode</label>
          <div style={{ display: 'flex', gap: '8px' }}>
            {(['translate', 'rotate', 'scale'] as const).map(mode => (
              <button
                key={mode}
                onClick={() => setTransformMode(mode)}
                style={{
                  flex: 1,
                  padding: '8px',
                  backgroundColor: transformMode === mode ? '#3498db' : '#111',
                  color: 'white',
                  border: '1px solid #444',
                  borderRadius: '4px',
                  cursor: 'pointer',
                  textTransform: 'capitalize'
                }}
              >
                {mode}
              </button>
            ))}
          </div>
        </div>
        
        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <label>Model Path (relative to 3d-wallpaper)</label>
          <input 
            type="text" 
            value={modelPath}
            onChange={e => setModelPath(e.target.value)}
            style={{ padding: '8px', borderRadius: '4px', border: '1px solid #444', backgroundColor: '#111', color: '#fff' }}
          />
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <label>Light Position (X, Y, Z)</label>
            <button 
              onClick={() => setLightPos([10, 10, 5])}
              style={{ padding: '2px 8px', fontSize: '12px', backgroundColor: '#333', color: '#fff', border: '1px solid #555', borderRadius: '3px', cursor: 'pointer' }}
            >
              Reset Light
            </button>
          </div>
          <div style={{ display: 'flex', gap: '4px' }}>
            <input type="number" step="1" value={lightPos[0]} onChange={e => setLightPos([parseFloat(e.target.value) || 0, lightPos[1], lightPos[2]])} style={{ flex: 1, padding: '4px', backgroundColor: '#111', color: '#fff', border: '1px solid #444', width: 0 }} />
            <input type="number" step="1" value={lightPos[1]} onChange={e => setLightPos([lightPos[0], parseFloat(e.target.value) || 0, lightPos[2]])} style={{ flex: 1, padding: '4px', backgroundColor: '#111', color: '#fff', border: '1px solid #444', width: 0 }} />
            <input type="number" step="1" value={lightPos[2]} onChange={e => setLightPos([lightPos[0], lightPos[1], parseFloat(e.target.value) || 0])} style={{ flex: 1, padding: '4px', backgroundColor: '#111', color: '#fff', border: '1px solid #444', width: 0 }} />
          </div>
        </div>
        
        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <label>Exposure</label>
            <span>{exposure.toFixed(2)}</span>
          </div>
          <input 
            type="range" 
            min="0.1" max="5.0" step="0.1" 
            value={exposure} 
            onChange={e => setExposure(parseFloat(e.target.value))} 
            style={{ width: '100%' }}
          />
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <label>Mouse Sensitivity</label>
            <span>{sensitivity.toFixed(2)}</span>
          </div>
          <input 
            type="range" 
            min="0" max="2.0" step="0.05" 
            value={sensitivity} 
            onChange={e => setSensitivity(parseFloat(e.target.value))} 
            style={{ width: '100%' }}
          />
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <label>Max Rotation Offset (°)</label>
            <span>{maxRotationOffset.toFixed(0)}</span>
          </div>
          <input 
            type="range" 
            min="1" max="90" step="1" 
            value={maxRotationOffset} 
            onChange={e => setMaxRotationOffset(parseFloat(e.target.value))} 
            style={{ width: '100%' }}
          />
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <label>Object Rotation Multiplier</label>
            <span>{rotationMultiplier.toFixed(2)}</span>
          </div>
          <input 
            type="range" 
            min="0" max="5.0" step="0.1" 
            value={rotationMultiplier} 
            onChange={e => setRotationMultiplier(parseFloat(e.target.value))} 
            style={{ width: '100%' }}
          />
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <label>Background Type</label>
          <select 
            value={bgType}
            onChange={e => setBgType(e.target.value)}
            style={{ padding: '8px', borderRadius: '4px', border: '1px solid #444', backgroundColor: '#111', color: '#fff' }}
          >
            <option value="color">Solid Color</option>
            <option value="image">Image Path</option>
          </select>
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <label>Rendering Quality Tier</label>
          <select 
            value={qualityTier}
            onChange={e => setQualityTier(e.target.value as 'low' | 'balanced' | 'high')}
            style={{ padding: '8px', borderRadius: '4px', border: '1px solid #444', backgroundColor: '#111', color: '#fff' }}
          >
            <option value="low">Low (Fast, No IBL)</option>
            <option value="balanced">Balanced</option>
            <option value="high">High (Full PBR)</option>
          </select>
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          <label>Background Value {bgType === 'color' ? '(Hex)' : '(Relative Path)'}</label>
          <input 
            type="text" 
            value={bgValue}
            onChange={e => setBgValue(e.target.value)}
            style={{ padding: '8px', borderRadius: '4px', border: '1px solid #444', backgroundColor: '#111', color: '#fff' }}
          />
        </div>

        <button 
          onClick={handleUpdate}
          style={{ padding: '10px', backgroundColor: '#3498db', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}
        >
          Update Scene
        </button>

        <button 
          onClick={handleApplyWallpaper}
          style={{ padding: '10px', backgroundColor: '#2ecc71', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer', marginTop: '10px' }}
        >
          Apply as Wallpaper
        </button>
        
        <button 
          onClick={handleReset}
          style={{ padding: '10px', backgroundColor: '#e74c3c', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}
        >
          Reset Object Transform
        </button>
        
        <button 
          onClick={() => setResetCameraTrigger(t => t + 1)}
          style={{ padding: '10px', backgroundColor: '#95a5a6', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}
        >
          Reset View
        </button>
        
        {status && <div style={{ color: '#88ee88', fontSize: '14px' }}>{status}</div>}
      </div>
    </div>
  )
}

export default App
