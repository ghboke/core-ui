import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { copyFileSync, existsSync } from 'node:fs'
import { resolve } from 'node:path'

// GitHub Pages 子路径部署：生产构建走 /core-ui/，本地 dev 仍是 /。
// 可通过环境变量 VITE_BASE 覆盖（自定义域名时设 VITE_BASE=/）。
export default defineConfig(({ mode }) => ({
  base: process.env.VITE_BASE ?? (mode === 'production' ? '/core-ui/' : '/'),
  plugins: [
    react(),
    {
      // SPA 兜底：把 dist/index.html 复制成 dist/404.html
      // GitHub Pages 对未知路径返回 404.html，同一个 React 应用接管后路由正常工作
      name: 'spa-pages-fallback',
      apply: 'build',
      closeBundle() {
        const src = resolve('dist/index.html')
        const dst = resolve('dist/404.html')
        if (existsSync(src)) copyFileSync(src, dst)
      },
    },
  ],
  build: {
    chunkSizeWarningLimit: 1000,
  },
}))
