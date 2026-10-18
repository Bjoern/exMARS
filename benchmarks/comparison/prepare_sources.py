"""Unpack the supplied source snapshot; optionally fetch public sources instead."""
import argparse,hashlib,json,shutil,tarfile,urllib.request,zipfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--snapshot',type=Path,help='benchmark-sources.zip supplied beside the patches')
p.add_argument('--pmars-source',type=Path,help='Required without a snapshot: local pMARS src directory')
a=p.parse_args();root=Path(__file__).resolve().parents[2];out=root/'dist/comparison-sources';out.mkdir(parents=True,exist_ok=True)
meta=json.loads((Path(__file__).parent/'source-provenance.json').read_text())
if a.snapshot:
    with zipfile.ZipFile(a.snapshot) as z:
        for member in z.namelist():
            target=(out/member).resolve()
            if not target.is_relative_to(out.resolve()):raise ValueError('Unsafe archive member')
        z.extractall(out)
else:
    if not a.pmars_source:p.error('--pmars-source is required when downloading sources')
    jobs=[('exhaust.tar.gz','http://distcache.freebsd.org/ports-distfiles/exhaust-1.9.2.tar.gz'),
          ('fmars.tar.bz2','http://distcache.freebsd.org/ports-distfiles/fmars-0.0.207.tar.bz2')]
    for name,url in jobs:
        data=urllib.request.urlopen(url,timeout=45).read()
        if hashlib.sha256(data).hexdigest()!=meta['archives'][name]:raise ValueError('Archive checksum mismatch: '+name)
        archive=out/name;archive.write_bytes(data)
        with tarfile.open(archive) as t:t.extractall(out,filter='data')
    for repo,name,commit in [('martinus/exhaust-ma','exhaust-ma-master',meta['martinus/exhaust-ma']['commit']),('martinus/qmars','qmars-master',meta['martinus/qmars']['commit']),('martinus/exMARS','original-exmars',meta['original-exmars']['commit'])]:
        archive=out/(name+'.zip');archive.write_bytes(urllib.request.urlopen('https://codeload.github.com/'+repo+'/zip/'+commit,timeout=45).read())
        with zipfile.ZipFile(archive) as z:
            prefix=z.namelist()[0].split('/')[0]+'/'
            for member in z.infolist():
                relative=member.filename.removeprefix(prefix)
                target=(out/name/relative).resolve()
                if not target.is_relative_to((out/name).resolve()):raise ValueError('Unsafe archive member')
                if not member.is_dir():target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(member))
    shutil.copytree(a.pmars_source,out/'pmars-local',dirs_exist_ok=True)
    s=out/'fmars-0.0.207/srfi-26.scm';s.write_text(s.read_text().replace('(use-modules (ice-9 syncase))',''))
    shutil.copytree(out/'fmars-0.0.207',out/'fmars-corrected',dirs_exist_ok=True)
    s=out/'fmars-corrected/insn.scm';text=s.read_text();start=text.index('(define (make-div/mod');end=text.index('(define (mov-fields',start)
    text=text[:start]+text[start:end].replace('(if_ dst','(if_ src').replace('(arfunc src dst)','(arfunc dst src)').replace('(map cadr fields)','(map car fields)')+text[end:];s.write_text(text)
    print('Run prepare_fmars_set.py and generate_fmars.sh for both fMARS directories.')
# Verify the local pMARS reference so substituting a different tree is explicit.
expected=meta['pmars']['source_sha256']
for name,digest in expected.items():
    if hashlib.sha256((out/'pmars-local'/name).read_bytes()).hexdigest()!=digest:raise ValueError('pMARS source differs: '+name)
print('Sources prepared in',out)
