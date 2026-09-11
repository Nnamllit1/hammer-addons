"""Publish a tested tagged release. Credentials come only from GH_TOKEN/GITHUB_TOKEN."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request

class Client:
    def __init__(self, token): self.token = token
    def request(self, method, url, data=None, binary=False):
        if urllib.parse.urlparse(url).hostname not in {'api.github.com', 'uploads.github.com'}:
            raise ValueError('Unexpected GitHub API host')
        headers = {'Authorization': 'Bearer '+self.token, 'Accept': 'application/vnd.github+json',
                   'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'hammer-addons-release'}
        if data is not None:
            headers['Content-Type'] = 'application/octet-stream' if binary else 'application/json'
            if not binary: data = json.dumps(data).encode()
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        with urllib.request.urlopen(req, timeout=120) as response:
            return json.load(response)

def publish(repo, version, directory, notes, client):
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo): raise ValueError('Invalid repository')
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9]+(?:\.[a-z0-9]+)*)?', version): raise ValueError('Invalid version')
    manifest = json.loads((directory/'release.json').read_text())
    if manifest['version'] != version or not re.fullmatch('[0-9a-f]{40}', manifest['commit']): raise ValueError('Invalid release provenance')
    expected = {f'hammer-addons-{version}-windows-x64.zip', f'hammer-addons-{version}-sdk.zip', 'release.json'}
    if set(manifest['archives']) != expected - {'release.json'}: raise ValueError('Unexpected archives')
    sums = {}
    for line in (directory/'SHA256SUMS.txt').read_text().splitlines():
        digest, name = line.split('  ', 1)
        if name not in expected or name in sums or not re.fullmatch('[0-9a-f]{64}', digest): raise ValueError('Invalid checksums')
        if hashlib.sha256((directory/name).read_bytes()).hexdigest() != digest: raise ValueError('Checksum mismatch')
        sums[name] = digest
    if set(sums) != expected: raise ValueError('Incomplete checksums')
    api = 'https://api.github.com/repos/' + repo
    tag = client.request('GET', api+'/git/ref/tags/'+version)['object']
    if tag['type'] == 'tag': tag = client.request('GET', api+'/git/tags/'+tag['sha'])['object']
    if tag['type'] != 'commit' or tag['sha'] != manifest['commit']: raise ValueError('Tag does not match built commit')
    try: release = client.request('GET', api+'/releases/tags/'+version)
    except urllib.error.HTTPError as error:
        if error.code != 404: raise
        release = client.request('POST', api+'/releases', {'tag_name': version, 'name': 'Hammer Addons '+version,
            'target_commitish': manifest['commit'], 'body': notes, 'draft': True, 'prerelease': '-' in version})
    upload = release['upload_url'].split('{')[0]
    existing = {a['name']: a for a in release['assets']}
    for name in sorted(expected | {'SHA256SUMS.txt'}):
        data = (directory/name).read_bytes(); digest = 'sha256:'+hashlib.sha256(data).hexdigest()
        if name in existing:
            if existing[name].get('digest') != digest: raise ValueError('Existing release asset differs; publish a new version')
            continue
        if not release['draft']: raise ValueError('Published release is missing assets; publish a new version')
        result = client.request('POST', upload+'?'+urllib.parse.urlencode({'name': name}), data, binary=True)
        if result.get('digest') != digest: raise ValueError('Uploaded asset checksum does not match')
    if release['draft']:
        release = client.request('PATCH', api+'/releases/'+str(release['id']), {'draft': False, 'make_latest': 'false' if '-' in version else 'true'})
    print(release['html_url'])

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', required=True); p.add_argument('--version', required=True)
    p.add_argument('--directory', type=Path, required=True); p.add_argument('--notes', type=Path, required=True)
    args = p.parse_args()
    token = os.environ.get('GH_TOKEN') or os.environ.get('GITHUB_TOKEN')
    if not token: p.error('GH_TOKEN or GITHUB_TOKEN is required')
    try: publish(args.repo, args.version, args.directory, args.notes.read_text(encoding='utf-8'), Client(token))
    except urllib.error.HTTPError as error: raise SystemExit(f'GitHub release request failed: HTTP {error.code}') from None
