# dummy (for now) Python distutils setup thing for hatari-ui

from distutils.core import setup

setup(name = 'hatari-ui',
      version = '0.6',
      scripts = ['hatari-ui.py', 'debugui.py', 'dialogs.py',
          'hatari-concole.py', 'hatari.py', 'config.py'],
      data_files = [
          ('share/pixmaps',      ['hatari-icon.png']),
          ('share/applications', ['hatari-ui.desktop']),
      ]
)
