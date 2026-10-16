import urllib.request
import re
import json
import time
import os
import concurrent.futures

base_url = 'https://www.cs2d.com/'
hdr = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/115.0.0.0 Safari/537.36'}

def fetch_url(url):
    req = urllib.request.Request(url, headers=hdr)
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=12) as resp:
                return resp.read().decode('latin1', errors='ignore')
        except Exception:
            time.sleep(0.4 * (attempt + 1))
    return ''

def parse_box(html, anchor_name):
    # Anchor can be <a name="cmd"> or <a name="hook">
    pattern = rf'<a name=.{re.escape(anchor_name)}.*?>(.*?)<div id=.disqus_thread.>'
    m = re.search(pattern, html, re.DOTALL)
    if not m:
        # fallback: without anchor
        pattern_fallback = r'<h2>(?:<img[^>]+>\s*)*([^<]+)</h2>\s*<div class=.box.>(.*?)<div id=.disqus_thread.>'
        m_fb = re.search(pattern_fallback, html, re.DOTALL)
        if not m_fb:
            return {}
        title = m_fb.group(1).strip()
        chunk = m_fb.group(2)
    else:
        chunk = m.group(1)
        title_m = re.search(r'<h2>(?:<img[^>]+>\s*)*([^<]+)</h2>', chunk)
        title = title_m.group(1).strip() if title_m else ''

    categories = []
    cat_m = re.search(r'<h3>Categories</h3>\s*<div[^>]*>(.*?)</div>', chunk, re.DOTALL)
    if cat_m:
        categories = [re.sub(r'<[^>]+>', '', c).strip() for c in cat_m.group(1).split(',') if c.strip()]

    parameters = []
    param_m = re.search(r'<h3>Parameters</h3>\s*(.*?)(?=<h3>|<div id=.disqus_thread.|$)', chunk, re.DOTALL)
    if param_m:
        params_raw = re.findall(r'<li>(.*?)</li>', param_m.group(1), re.DOTALL)
        if not params_raw:
            # Maybe single text parameter
            p_text = re.sub(r'<[^>]+>', ' ', param_m.group(1)).strip()
            if p_text:
                parameters = [re.sub(r'\s+', ' ', p_text)]
        else:
            for p in params_raw:
                p_clean = re.sub(r'<[^>]+>', ' ', p)
                p_clean = re.sub(r'\s+', ' ', p_clean).strip()
                if p_clean:
                    parameters.append(p_clean)

    ret_val = ''
    ret_m = re.search(r'<h3>Return Values?</h3>\s*(.*?)(?=<h3>|<div id=.disqus_thread.|$)', chunk, re.DOTALL)
    if ret_m:
        ret_val = re.sub(r'<[^>]+>', ' ', ret_m.group(1))
        ret_val = re.sub(r'\s+', ' ', ret_val).strip()

    info_val = ''
    info_m = re.search(r'<h3>Info</h3>\s*(.*?)(?=<h3>|<div id=.disqus_thread.|$)', chunk, re.DOTALL)
    if info_m:
        t = re.sub(r'<br\s*/?>', '\n', info_m.group(1))
        t = re.sub(r'<[^>]+>', ' ', t)
        t = '\n'.join([l.strip() for l in t.split('\n') if l.strip()])
        info_val = t

    return {
        'name': title,
        'categories': categories,
        'parameters': parameters,
        'return_value': ret_val,
        'info': info_val
    }

def main():
    print('Starting full scrape of CS2D reference with rate control...')

    html_cmds = fetch_url(base_url + 'help.php?cat=all')
    cmd_names = sorted(list(set(re.findall(r'help\.php\?cat=all&(?:amp;)?cmd=([^"#]+)#cmd', html_cmds))))
    print(f'Found {len(cmd_names)} console commands.')

    html_hooks = fetch_url(base_url + 'help.php?hookcat=all')
    hook_names = sorted(list(set(re.findall(r'help\.php\?hookcat=all&(?:amp;)?hook=([^"#]+)#hook', html_hooks))))
    print(f'Found {len(hook_names)} Lua hooks.')

    html_lua = fetch_url(base_url + 'help.php?luacat=all')
    lua_names = sorted(list(set(re.findall(r'help\.php\?luacat=all&(?:amp;)?luacmd=([^"#]+)#cmd', html_lua))))
    print(f'Found {len(lua_names)} Lua commands.')

    db = {
        'console_commands': {},
        'lua_hooks': {},
        'lua_commands': {}
    }

    def fetch_cmd(name):
        time.sleep(0.05)
        url = f'{base_url}help.php?cat=all&cmd={name}#cmd'
        html = fetch_url(url)
        data = parse_box(html, 'cmd')
        data['url'] = url
        return name, data

    def fetch_hook(name):
        time.sleep(0.05)
        url = f'{base_url}help.php?hookcat=all&hook={name}#hook'
        html = fetch_url(url)
        data = parse_box(html, 'hook')
        data['url'] = url
        return name, data

    def fetch_lua_cmd(name):
        time.sleep(0.05)
        url = f'{base_url}help.php?luacat=all&luacmd={name}#cmd'
        html = fetch_url(url)
        data = parse_box(html, 'cmd')
        data['url'] = url
        return name, data

    print('Fetching details concurrently (8 workers)...')
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for name, data in pool.map(fetch_cmd, cmd_names):
            db['console_commands'][name] = data
        print('Console commands scraped.')

        for name, data in pool.map(fetch_hook, hook_names):
            db['lua_hooks'][name] = data
        print('Hooks scraped.')

        for name, data in pool.map(fetch_lua_cmd, lua_names):
            db['lua_commands'][name] = data
        print('Lua commands scraped.')

    out_file = 'docs/cs2d_help_db_complete.json'
    with open(out_file, 'w', encoding='utf-8') as f:
        json.dump(db, f, indent=2, ensure_ascii=False)
    print(f'Successfully wrote complete database to {out_file} (size: {os.path.getsize(out_file)} bytes).')

if __name__ == '__main__':
    main()
