"""Exercise real offline signing, tamper rejection and pre-load signature checks."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin',type=Path,required=True)
    args=parser.parse_args()
    binaries=args.bin.resolve()
    root=Path(__file__).resolve().parents[1]
    (root/'build/tests').mkdir(exist_ok=True)
    checks=0
    def run(command,code=0):
        nonlocal checks
        result=subprocess.run([str(v) for v in command],stdin=subprocess.DEVNULL,capture_output=True,text=True,encoding='utf-8',timeout=30)
        assert result.returncode==code,(command,result.returncode,result.stdout,result.stderr)
        checks+=1
        return result.stdout+result.stderr
    tool=binaries/'addon_sign.exe'
    with tempfile.TemporaryDirectory(prefix='signing space-',dir=root/'build/tests') as folder:
        work=Path(folder)
        key=work/'publisher.hakey'
        output=run([tool,'keygen',key])
        fingerprint=re.search(r'fingerprint: ([a-f0-9]{64})',output)[1]
        protected=key.read_bytes()
        run([tool,'keygen',key],1)
        assert key.read_bytes()==protected
        other=work/'other.hakey'
        other_fp=re.search(r'fingerprint: ([a-f0-9]{64})',run([tool,'keygen',other]))[1]
        assert fingerprint!=other_fp
        pristine=work/'original'
        pristine.mkdir()
        shutil.copy2(root/'addons/hello/addon.ini',pristine/'addon.ini')
        shutil.copy2(binaries/'hello.dll',pristine/'hello.dll')
        (pristine/'dependency.dll').write_bytes(b'test dependency bytes')
        (pristine/'data').mkdir()
        (pristine/'data/empty.txt').touch()
        (pristine/'data/example.txt').write_text('included asset\n')
        run([tool,'verify',pristine],2)
        run([tool,'sign',pristine,'--key',key,'--name','Example Author','--contact','author@example.invalid','--website','https://example.invalid/author'])
        text=(pristine/'addon.signature').read_text()
        output=run([tool,'verify',pristine,'--publisher',fingerprint])
        assert 'Example Author' in output and 'author@example.invalid' in output and 'https://example.invalid/author' in output
        run([tool,'verify',pristine,'--publisher',other_fp],1)
        run([tool,'sign',pristine,'--key',key,'--name','Other'],1)
        assert (pristine/'addon.signature').read_text()==text
        def case(name):
            destination=work/name/'addons/hello'
            shutil.copytree(pristine,destination)
            return destination
        def host(package,accepted):
            output=run([binaries/'ha_host.exe',package.parent.parent],0 if accepted else 1)
            status=json.loads(next(line[7:] for line in output.splitlines() if line.startswith('STATUS ')))['addons'][0]
            assert (status['state']=='Loaded')==accepted,output
            if not accepted:assert 'Hello from a native' not in output,output
            return status
        valid=case('valid')
        host(valid,False) # A self-created signature cannot bypass first-use trust.
        run([tool,'pin',valid,'--publisher',fingerprint,'--store',valid.parent.parent/'publisher-pins'])
        status=host(valid,True)
        assert status['publisher_fingerprint']==fingerprint and status['publisher']=='Example Author'
        pinned=case('pinned')
        pins=pinned.parent.parent/'publisher-pins'
        run([tool,'pin',pinned,'--publisher',other_fp,'--store',pins],1)
        run([tool,'pin',pinned,'--publisher',fingerprint,'--store',pins])
        run([tool,'pin',pinned,'--publisher',fingerprint,'--store',pins],1)
        assert host(pinned,True)['signature']=='Valid (locally pinned key)'
        (pinned/'addon.signature').unlink()
        assert host(pinned,False)['signature']=='Unsigned'
        run([tool,'sign',pinned,'--key',other,'--name','Example Author'])
        assert host(pinned,False)['publisher_fingerprint']==other_fp
        (pinned/'addon.signature').unlink()
        run([tool,'sign',pinned,'--key',key,'--name','Updated Display Name'])
        assert host(pinned,True)['signature']=='Valid (locally pinned key)'
        (pins/'hello.sha256').write_text('broken pin')
        host(pinned,False)
        denied=case('dllmain-denied')
        shutil.copy2(binaries/'signing_probe.dll',denied/'hello.dll')
        marker=work/'dllmain.marker'
        previous=os.environ.get('HA_SIGNATURE_PROBE_MARKER')
        os.environ['HA_SIGNATURE_PROBE_MARKER']=str(marker)
        try:
            host(denied,False)
            assert not marker.exists(), 'Invalid signature allowed DllMain to run'
            (denied/'addon.signature').unlink()
            run([tool,'sign',denied,'--key',key,'--name','Example'])
            run([tool,'pin',denied,'--publisher',fingerprint,'--store',denied.parent.parent/'publisher-pins'])
            host(denied,False) # Valid trusted signature; deliberately invalid addon ABI.
            assert marker.exists(), 'DllMain positive control did not execute'
        finally:
            if previous is None:os.environ.pop('HA_SIGNATURE_PROBE_MARKER',None)
            else:os.environ['HA_SIGNATURE_PROBE_MARKER']=previous
        mutations={
            'changed-dll':lambda p:(p/'hello.dll').write_bytes(b'changed binary'),
            'changed-dependency':lambda p:(p/'dependency.dll').write_bytes(b'changed dependency'),
            'changed-asset':lambda p:(p/'data/example.txt').write_text('modified'),
            'missing-dependency':lambda p:(p/'dependency.dll').unlink(),
            'extra-dll':lambda p:(p/'extra.dll').write_bytes(b'extra'),
            'changed-manifest':lambda p:(p/'addon.ini').write_text((p/'addon.ini').read_text().replace('enabled=true','enabled=false')),
            'changed-author':lambda p:(p/'addon.signature').write_text(text.replace('Example Author'.encode().hex(),'Another Author'.encode().hex())),
            'changed-contact':lambda p:(p/'addon.signature').write_text(text.replace('author@example.invalid'.encode().hex(),'other@example.invalid'.encode().hex())),
            'bad-format':lambda p:(p/'addon.signature').write_text(text.replace('SIGNATURE-1','SIGNATURE-2')),
            'trailing-data':lambda p:(p/'addon.signature').write_text(text+'extra\n'),
            'truncated':lambda p:(p/'addon.signature').write_text(text[:-10]),
            'duplicate-field':lambda p:(p/'addon.signature').write_text(text.replace('name=','name=\nname=')),
            'changed-signature':lambda p:(p/'addon.signature').write_text(text[:text.index('signature=')]+ 'signature='+'00'*64+'\n'),
            'oversized-signature':lambda p:(p/'addon.signature').write_bytes(b'x'*(1024*1024+1)),
        }
        for name,mutate in mutations.items():
            package=case(name)
            mutate(package)
            run([tool,'verify',package],1)
            host(package,False)
        linked=case('hardlink')
        (linked/'data/example.txt').unlink()
        os.link(pristine/'data/example.txt',linked/'data/example.txt')
        run([tool,'verify',linked],1)
        (linked/'data/example.txt').unlink() # Restore pristine's link count.
        absent=case('removed-signature')
        (absent/'addon.signature').unlink()
        run([tool,'verify',absent],2)
        run([tool,'verify',absent,'--publisher',fingerprint],2)
        assert host(absent,False)['signature']=='Unapproved (unsigned)'
        approvals=absent.parent.parent/'local-approvals'
        run([tool,'approve',absent,'--store',approvals])
        assert host(absent,True)['signature']=='Approved locally (unsigned)'
        (absent/'data/example.txt').write_text('approved package changed')
        host(absent,False)
        run([tool,'approve',absent,'--store',approvals])
        assert host(absent,True)['signature']=='Approved locally (unsigned)'
        receipt=approvals/'hello.approval'
        receipt.write_bytes(b'corrupt receipt')
        host(absent,False)
        run([tool,'approve',absent,'--store',approvals])
        run([tool,'approve',pristine,'--store',approvals],1) # Cannot override a publisher signature.

        (absent/'inside.hakey').write_bytes(protected)
        run([tool,'sign',absent,'--key',absent/'inside.hakey','--name','Example'],1)
        (absent/'inside.hakey').unlink()
        broken=work/'broken.hakey'
        broken.write_bytes(protected[:-20]+b'\0'*20)
        run([tool,'sign',absent,'--key',broken,'--name','Example'],1)
        run([tool,'sign',absent,'--key',key,'--name',''],1)
        run([tool,'sign',absent,'--key',key,'--name','invalid\nname'],1)
        run([tool,'sign',absent,'--key',key,'--name','New Display Name'])
        assert fingerprint in run([tool,'verify',absent,'--publisher',fingerprint])
        password_name='HA_FIXTURE_KEY_PASSWORD'
        old_password=os.environ.get(password_name)
        password=os.urandom(32).hex()
        os.environ[password_name]=password
        portable=work/'portable.hapkey'
        imported=work/'imported.hakey'
        try:
            output=run([tool,'export-key',key,'--output',portable,'--password-env',password_name])
            assert fingerprint in output and password not in output and password.encode() not in portable.read_bytes()
            original_portable=portable.read_bytes()
            run([tool,'export-key',key,'--output',portable,'--password-env',password_name],1)
            assert portable.read_bytes()==original_portable
            os.environ[password_name]='wrong password that is long enough'
            run([tool,'import-key',portable,'--output',imported,'--password-env',password_name],1)
            assert not imported.exists()
            os.environ[password_name]=password
            assert fingerprint in run([tool,'import-key',portable,'--output',imported,'--password-env',password_name])
            for i,offset in enumerate([0,len(b'HAMMER-ADDONS-PORTABLE-KEY-1\n'),60,90,len(original_portable)-1]):
                damaged=bytearray(original_portable);damaged[offset]^=1
                bad=work/f'tampered-{i}.hapkey';bad.write_bytes(damaged)
                run([tool,'import-key',bad,'--output',work/f'bad-import-{i}.hakey','--password-env',password_name],1)
            ci=case('ci-sign')
            (ci/'addon.signature').unlink()
            key.rename(work/'local-key-not-available.hakey')
            assert fingerprint in run([tool,'sign',ci,'--key',portable,'--name','CI author','--password-env',password_name])
            assert fingerprint in run([tool,'verify',ci,'--publisher',fingerprint])
            local=case('imported-key-sign')
            (local/'addon.signature').unlink()
            assert fingerprint in run([tool,'sign',local,'--key',imported,'--name','Imported key'])
            os.environ.pop(password_name)
            run([tool,'import-key',portable,'--output',work/'missing-password.hakey','--password-env',password_name],1)
        finally:
            if old_password is None:os.environ.pop(password_name,None)
            else:os.environ[password_name]=old_password
    print(f'Offline signing passed: {checks} command checks, identity binding, tamper rejection, expected-key checks, protected keys, local approvals, CI key export/import and runtime pre-load rejection.')

if __name__=='__main__':main()
