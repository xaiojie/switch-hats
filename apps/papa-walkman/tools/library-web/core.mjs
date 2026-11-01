export function resourceUrl(value, base) {
  if (!value) return '';
  const url = new URL(String(value), base);
  if (!['https:', 'http:'].includes(url.protocol) || url.username || url.password) throw new Error('曲库包含无效的资源地址');
  return url.href;
}

function normalize(raw, index, base) {
  const audio = resourceUrl(raw.audio || raw.url || raw.path, base);
  if (!audio) throw new Error(`第 ${index + 1} 首歌曲缺少音频地址`);
  const extension = new URL(audio).pathname.split('.').pop().toLowerCase();
  const format = String(raw.format || extension).toLowerCase();
  const duration = Number(raw.duration);
  return {...raw, key: `${raw.id || index}:${audio}`, id: raw.id || '', audio,
    title: String(raw.title || decodeURIComponent(new URL(audio).pathname.split('/').pop())),
    artist: String(raw.artist || ''), album: String(raw.album || ''),
    duration: Number.isFinite(duration) && duration > 0 ? duration : null,
    cover: resourceUrl(raw.cover, base), lyrics: resourceUrl(raw.lyrics, base), format,
    playable: format === 'mp3' && extension === 'mp3' && raw.switchPlayable !== false,
  };
}

export function parseCatalog(text, url) {
  text = text.replace(/^\uFEFF/, '').trim();
  if (text.startsWith('{')) {
    const data = JSON.parse(text);
    if (!Array.isArray(data.tracks)) throw new Error('JSON 缺少 tracks 数组');
    if (data.tracks.length > 10000) throw new Error('曲库超过 10000 首上限');
    const base = resourceUrl(data.base || url, url);
    return {name: String(data.name || 'Kiro 音乐库'), version: data.version ?? null, generated: data.generated || '',
      type: 'JSON', stats: data.stats || null, tracks: data.tracks.map((t, i) => normalize(t, i, base))};
  }
  if (!text.startsWith('#EXTM3U')) throw new Error('不是有效的 JSON 曲库或 M3U 播放列表');
  if (text.includes('#EXT-X-')) throw new Error('这是 HLS 播放列表，不是歌曲曲库');
  const tracks = []; let metadata = {}, album = '';
  for (const line of text.split(/\r?\n/).map(x => x.trim()).filter(Boolean)) {
    if (line.startsWith('#EXTGRP:') || line.startsWith('#EXTALB:')) album = line.slice(line.indexOf(':') + 1).trim();
    else if (line.startsWith('#EXTINF:')) {
      const comma = line.indexOf(','); const info = line.slice(8, comma);
      const title = comma >= 0 ? line.slice(comma + 1) : '';
      const split = title.indexOf(' - ');
      metadata = {title: split >= 0 ? title.slice(split + 3) : title, artist: split >= 0 ? title.slice(0, split) : '', duration: Number.parseFloat(info), cover: info.match(/tvg-logo="([^"]*)"/)?.[1] || ''};
    } else if (line.startsWith('#PAPA-TRACK ')) {
      try { metadata = {...metadata, ...JSON.parse(line.slice(12))}; } catch { /* A malformed optional extension must not execute code. */ }
    } else if (!line.startsWith('#')) {
      tracks.push(normalize({...metadata, audio: line, album: metadata.album || album}, tracks.length, url)); metadata = {};
      if (tracks.length > 10000) throw new Error('曲库超过 10000 首上限');
    }
  }
  return {name: 'Kiro 播放列表', type: 'M3U', version: null, generated: '', tracks};
}

export function summarize(tracks) {
  return {total: tracks.length, artist: tracks.filter(t => t.artist.trim()).length,
    duration: tracks.filter(t => t.duration > 0).length, cover: tracks.filter(t => t.cover).length,
    lyrics: tracks.filter(t => t.lyrics).length, playable: tracks.filter(t => t.playable).length};
}

export function parseLyrics(text) {
  text = text.replace(/^\uFEFF/, '');
  if (/^\s*(<!doctype|<html)/i.test(text)) throw new Error('歌词地址返回了网页');
  const offset = Number(text.match(/\[offset:([+-]?\d+)\]/i)?.[1] || 0);
  const lines = [];
  for (const line of text.split(/\r?\n/)) {
    const times = [...line.matchAll(/\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\]/g)];
    const words = line.replace(/\[[^\]]*\]/g, '').trim();
    for (const time of times) lines.push({time: Math.max(0, Number(time[1]) * 60000 + Number(time[2]) * 1000 + Number((time[3] || '').padEnd(3, '0')) - offset) / 1000, text: words});
  }
  lines.sort((a, b) => a.time - b.time);
  return {synced: lines.length > 0, lines, plain: text.split(/\r?\n/).filter(line => !/^\s*\[[a-z]+:/i.test(line)).join('\n').trim()};
}

export function timeLabel(value) {
  if (!Number.isFinite(value) || value < 0) return '--:--';
  const seconds = Math.floor(value); return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
}
