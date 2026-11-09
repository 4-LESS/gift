const CACHE_NAME = 'holodeck-cache-v2.1';
const ASSETS = [
  './',
  './index.html',
  './manifest.json',
  './qr_setup.svg'
];

// Dominios de anuncios y telemetría a bloquear a nivel de Service Worker
const AD_DOMAINS = [
  'doubleclick.net',
  'googlesyndication.com',
  'googleadservices.com',
  'adservice.google.',
  'youtube.com/api/stats/ads',
  'youtube.com/pagead/',
  'youtube.com/ptracking',
  's.youtube.com/api/stats/qoe'
];

self.addEventListener('install', event => {
  self.skipWaiting();
  event.waitUntil(
    caches.open(CACHE_NAME).then(cache => cache.addAll(ASSETS))
  );
});

self.addEventListener('activate', event => {
  event.waitUntil(
    caches.keys().then(keys =>
      Promise.all(
        keys.filter(k => k !== CACHE_NAME).map(k => caches.delete(k))
      )
    ).then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', event => {
  const url = event.request.url;

  // 1. Bloquear peticiones de anuncios conocidas
  if (AD_DOMAINS.some(domain => url.includes(domain))) {
    event.respondWith(new Response('', { status: 204, statusText: 'Blocked by HoloDeck AdFilter' }));
    return;
  }

  // Solo cachear peticiones GET propias (HTML/CSS/SVG/Manifest)
  if (event.request.method !== 'GET' || !url.startsWith(self.location.origin)) {
    return;
  }

  // 2. Estrategia Network-First:
  // Si hay internet, descarga la última versión de GitHub Pages y actualiza el caché.
  // Si NO hay internet, sirve instantáneamente la última versión guardada en el celular.
  event.respondWith(
    fetch(event.request)
      .then(networkResponse => {
        if (networkResponse && networkResponse.status === 200) {
          const responseClone = networkResponse.clone();
          caches.open(CACHE_NAME).then(cache => {
            cache.put(event.request, responseClone);
          });
        }
        return networkResponse;
      })
      .catch(() => {
        return caches.match(event.request).then(cachedResponse => {
          return cachedResponse || caches.match('./index.html');
        });
      })
  );
});
