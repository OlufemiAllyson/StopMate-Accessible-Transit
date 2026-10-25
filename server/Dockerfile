FROM node:22-bookworm-slim
WORKDIR /app
ENV NODE_ENV=production HOST=0.0.0.0 PORT=8080
COPY package.json package-lock.json ./
RUN npm ci --omit=dev --ignore-scripts && npm cache clean --force
COPY index.js announcement-store.js stops.json ./
COPY public ./public
USER node
EXPOSE 8080
CMD ["node", "index.js"]
