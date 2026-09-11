"""Offline regression tests for the release publication contract."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import urllib.error

spec = importlib.util.spec_from_file_location('publish', Path(__file__).with_name('publish-release.py'))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
TAG = 'v0.1.0-alpha.1'
SHA = 'a'*40

class API:
    def __init__(self, existing=False, draft=True):
        self.calls=[]; self.exists=existing; self.bad_digest=False
        self.release={'id':1,'draft':draft,'assets':[],'upload_url':'https://uploads.github.com/repos/owner/repo/releases/1/assets{?name}', 'html_url':'https://github.com/owner/repo/releases/tag/'+TAG}
    def request(self, method, url, data=None, binary=False):
        self.calls.append((method,url))
        if '/git/ref/' in url: return {'object':{'type':'commit','sha':SHA}}
        if method=='GET':
            if not self.exists: raise urllib.error.HTTPError(url,404,'missing',{},None)
            return self.release
        if binary:
            return {'digest':'sha256:'+('0'*64 if self.bad_digest else hashlib.sha256(data).hexdigest())}
        if method=='POST': return self.release
        self.release['draft']=False
        return self.release

class Tests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.path=Path(self.temp.name)
        self.names=[f'hammer-addons-{TAG}-windows-x64.zip',f'hammer-addons-{TAG}-sdk.zip']
        for n in self.names:(self.path/n).write_bytes(b'fixture archive')
        (self.path/'release.json').write_text(json.dumps({'version':TAG,'commit':SHA,'archives':self.names}))
        self.checksums()
    def checksums(self):
        (self.path/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256((self.path/n).read_bytes()).hexdigest()+'  '+n+'\n' for n in self.names+['release.json']))
    def run_publish(self,api):
        with contextlib.redirect_stdout(io.StringIO()): m.publish('owner/repo',TAG,self.path,'notes',api)
    def test_draft_before_upload_publish_last(self):
        api=API();self.run_publish(api)
        self.assertEqual(api.calls[2][0],'POST')
        self.assertEqual(sum('uploads.github.com' in u for _,u in api.calls),4)
        self.assertEqual(api.calls[-1][0],'PATCH')
    def test_corrupt_local_archive_never_calls_api(self):
        (self.path/self.names[0]).write_bytes(b'corrupt');api=API()
        with self.assertRaisesRegex(ValueError,'Checksum'):self.run_publish(api)
        self.assertFalse(api.calls)
    def test_changed_tag_does_not_publish(self):
        manifest=json.loads((self.path/'release.json').read_text());manifest['commit']='b'*40
        (self.path/'release.json').write_text(json.dumps(manifest));self.checksums();api=API()
        with self.assertRaisesRegex(ValueError,'Tag does not match'):self.run_publish(api)
        self.assertEqual(len(api.calls),1)
    def test_failed_upload_stays_draft(self):
        api=API();api.bad_digest=True
        with self.assertRaisesRegex(ValueError,'Uploaded asset'):self.run_publish(api)
        self.assertTrue(api.release['draft']);self.assertFalse(any(v=='PATCH' for v,_ in api.calls))
    def test_published_release_cannot_be_mutated(self):
        api=API(True,False)
        with self.assertRaisesRegex(ValueError,'missing assets'):self.run_publish(api)
        self.assertTrue(all(v=='GET' for v,_ in api.calls))
    def test_identical_published_retry_is_read_only(self):
        api=API(True,False)
        api.release['assets']=[{'name':p.name,'digest':'sha256:'+hashlib.sha256(p.read_bytes()).hexdigest()} for p in self.path.iterdir()]
        self.run_publish(api);self.assertTrue(all(v=='GET' for v,_ in api.calls))
    def test_different_asset_requires_new_version(self):
        api=API(True);api.release['assets']=[{'name':'SHA256SUMS.txt','digest':'sha256:'+'0'*64}]
        with self.assertRaisesRegex(ValueError,'differs'):self.run_publish(api)
        self.assertTrue(all(v=='GET' for v,_ in api.calls))
    def test_path_traversal_checksum_rejected(self):
        (self.path/'SHA256SUMS.txt').write_text('0'*64+'  ../secret\n');api=API()
        with self.assertRaisesRegex(ValueError,'Invalid checksums'):self.run_publish(api)
        self.assertFalse(api.calls)

if __name__=='__main__':unittest.main()
