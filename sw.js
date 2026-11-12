const CACHE_NAME = 'holodeck-cache-v2.6';
const ASSETS = [
  './',
  './index.html',
  './manifest.json',
  './mensaje.json',
  './albumes.json',
  './qr_setup.svg'
];

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

  if (AD_DOMAINS.some(domain => url.includes(domain))) {
    event.respondWith(new Response('', { status: 204, statusText: 'Blocked by HoloDeck AdFilter' }));
    return;
  }

  if (event.request.method !== 'GET' || !url.startsWith(self.location.origin)) {
    return;
  }

  // Estrategia Network-First: siempre trae lo último de GitHub Pages y guarda respaldo para modo Offline
  event.respondWith(
    fetch(event.request, { cache: 'no-store' })
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
