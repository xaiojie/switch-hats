import argparse
from pathlib import Path

from switch_link.http import Application, make_server
from switch_link.discovery import local_addresses
from switch_link.library import HttpShare


def main():
    parser = argparse.ArgumentParser(description='Switch Link 本地管理服务')
    parser.add_argument('--host', default=None)
    parser.add_argument('--port', type=int, default=8088)
    parser.add_argument('--data', type=Path, default=Path(__file__).resolve().parent / 'data')
    parser.add_argument('--seed', action='append', default=[])
    parser.add_argument('--library', type=Path)
    parser.add_argument('--http-port', type=int, default=8080)
    parser.add_argument('--http-host', default='0.0.0.0')
    args = parser.parse_args()
    addresses = local_addresses()
    host = args.host or '127.0.0.1'
    existing_library = Path(__file__).resolve().parent.parent / 'game-library' / '游戏库'
    library_root = args.library or (existing_library if existing_library.is_dir() else args.data / 'library')
    app = Application(args.data, seeds=args.seed, library_root=library_root)
    app.http_share = HttpShare(app.library, host=args.http_host, port=args.http_port,
                               advertise=addresses[0] if addresses else '127.0.0.1')
    app.http_share.link_jobs = app.http_jobs
    app.http_share.music = app.music
    app.http_share.start()
    if app.http_share.owned:
        app.http_jobs.configure(app.http_share.url)
    elif app.http_share.external:
        # Keep a legacy server's active stream intact. Native capability URLs
        # use an independent HTTP channel; no address needs typing on Switch.
        app.control_share = HttpShare(app.library, host=args.http_host, port=args.http_port + 1,
                                      advertise=addresses[0] if addresses else '127.0.0.1')
        app.control_share.link_jobs = app.http_jobs
        app.control_share.music = app.music
        app.control_share.start(reuse=False)
        if app.control_share.owned:
            app.http_jobs.configure(app.control_share.url)
    server = make_server(app, host, args.port)
    print('Switch Link: http://%s:%d/' % (host, server.server_port), flush=True)
    print('HTTP library: ' + app.http_share.url + '/files/', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        app.close()


if __name__ == '__main__':
    main()
