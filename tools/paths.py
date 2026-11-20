"""Where WARP HUB finds the other projects on the PC.

WARP HUB lives in C:\\Workprojects\\AmigaSoft\\WarpHub; the game projects (their
private/ full packages, releases, pictures) and WARP-drawer.info are in
C:\\Workprojects\\AmigaGAMES. AMIGAGAMES in the environment overrides it.
Only the PC tools need it (not the catalog job on GitHub).
"""
import os

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMES = os.environ.get('AMIGAGAMES') or os.path.join(
    os.path.dirname(os.path.dirname(HERE)), 'AmigaGAMES')
