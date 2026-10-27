import os
import sys

if not __package__:
    # run as `python3 tools_port/bcfiles ...`: import the package from its parent
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path[:] = [p for p in sys.path if os.path.abspath(p or ".") != here]
    sys.path.insert(0, os.path.dirname(here))
    from bcfiles.cli import main
else:
    from .cli import main

main()
