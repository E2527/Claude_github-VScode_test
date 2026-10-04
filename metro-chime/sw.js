// Keeps the whole app on the device so the home-screen app starts and runs with no network.
// Cache first for speed and offline use; each launch also refreshes the cache in the background,
// so an update shows up on the launch after it was published.
const CACHE = 'metro-chime-v2';
const SHELL = [
  './', './index.html', './manifest.webmanifest', './icon-180.png', './icon-512.png',
  './fonts/fonts.css', './fonts/BigShouldersDisplay.woff2', './fonts/MartianMono.woff2',
  './fonts/ZenKakuGothicNew-500.woff2', './fonts/ZenKakuGothicNew-700.woff2',
];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET' || new URL(req.url).origin !== location.origin) return;
  e.respondWith(caches.open(CACHE).then(async cache => {
    const key = req.mode === 'navigate' ? './index.html' : req;
    const cached = await cache.match(key, { ignoreSearch: true });
    const refresh = fetch(req).then(res => {
      if (res.ok) cache.put(key, res.clone());
      return res;
    }).catch(() => null);
    if (cached) { e.waitUntil(refresh); return cached; }
    return (await refresh) || new Response('Offline', { status: 503 });
  }));
});
