import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

const backendPort = Number(process.env.VITE_BACKEND_PORT || '18080')

export default defineConfig({
  plugins: [react()],
  server: {
    port: 3000,
    open: false,
    strictPort: false,
    proxy: {
      '/api': {
        target: `http://localhost:${backendPort}`,
        changeOrigin: true,
      },
    },
  },
})
