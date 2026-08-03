import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import fs from 'fs'
import path from 'path'

// Custom plugin to intercept API calls
const scenePlugin = () => ({
  name: 'scene-plugin',
  configureServer(server: any) {
    server.middlewares.use((req: any, res: any, next: any) => {
      if (req.method === 'GET' && req.url) {
        // Exclude our API routes
        if (req.url.startsWith('/api/')) {
          return next()
        }
        
        // Remove query strings
        const urlPath = req.url.split('?')[0]
        
        // Try resolving against the parent directory
        const parentDir = path.resolve(__dirname, '..')
        const parentPath = path.resolve(parentDir, urlPath.slice(1))
        
        // Block path traversal outside the project
        if (!parentPath.startsWith(parentDir)) {
          return next()
        }
        
        if (fs.existsSync(parentPath) && fs.statSync(parentPath).isFile()) {
          const ext = path.extname(parentPath).toLowerCase()
          let contentType = 'application/octet-stream'
          if (ext === '.glb') contentType = 'model/gltf-binary'
          if (ext === '.gltf') contentType = 'model/gltf+json'
          if (ext === '.jpg' || ext === '.jpeg') contentType = 'image/jpeg'
          if (ext === '.png') contentType = 'image/png'
          
          res.setHeader('Content-Type', contentType)
          res.statusCode = 200
          fs.createReadStream(parentPath).pipe(res)
          return
        }
      }
      next()
    })

    server.middlewares.use('/api/scene', (req: any, res: any) => {
      if (req.method !== 'GET') {
        res.statusCode = 405
        res.end(JSON.stringify({ error: 'Method not allowed' }))
        return
      }
      try {
        const scenePath = path.resolve(__dirname, '../scene.json')
          if (fs.existsSync(scenePath)) {
            const data = fs.readFileSync(scenePath, 'utf-8')
            res.statusCode = 200
            res.setHeader('Content-Type', 'application/json')
            res.end(data)
          } else {
            res.statusCode = 404
            res.end(JSON.stringify({ error: 'Not found' }))
          }
      } catch (e: any) {
        res.statusCode = 500
        res.end(JSON.stringify({ error: e.message }))
      }
    })

    server.middlewares.use('/api/save', (req: any, res: any) => {
      if (req.method !== 'POST') {
        res.statusCode = 405
        res.end(JSON.stringify({ error: 'Method not allowed' }))
        return
      }
        let body = ''
        req.on('data', (chunk: any) => {
          body += chunk.toString()
        })
        req.on('end', () => {
          try {
            // Write to the parent directory (3d-wallpaper/scene.json)
            const scenePath = path.resolve(__dirname, '../scene.json')
            // Format it nicely
            const json = JSON.stringify(JSON.parse(body), null, 2)
            fs.writeFileSync(scenePath, json, 'utf-8')
            res.statusCode = 200
            res.end(JSON.stringify({ success: true }))
          } catch (e: any) {
            res.statusCode = 500
            res.end(JSON.stringify({ success: false, error: e.message }))
          }
        })
    })
  }
})

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), scenePlugin()],
})
