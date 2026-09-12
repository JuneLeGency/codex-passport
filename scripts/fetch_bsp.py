"""Fetch the official hardware dependency at its reviewed immutable commit."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
target=root/'upstream/ai-passport'
commit='f75873f1aab24ac4c0ba9394c131669f66cce650'
if target.exists():
    actual=subprocess.check_output(['git','-C',str(target),'rev-parse','HEAD'],text=True).strip()
    if actual!=commit:raise SystemExit('Existing BSP differs; preserve it and select the documented revision manually.')
else:
    target.parent.mkdir(exist_ok=True)
    subprocess.run(['git','clone','--no-checkout','https://github.com/folotoy/ai-passport.git',str(target)],check=True)
    subprocess.run(['git','-C',str(target),'checkout','--detach',commit],check=True)
print('Official BSP is pinned at',commit)
