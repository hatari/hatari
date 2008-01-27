from distutils.core import setup

setup(name = 'hatari-ui',
      version = '0.1',
      scripts = ['hatari-ui.py'],
      data_files = [
          ('share/pixmaps',      ['hatari_ui_icon.png']),
          ('share/applications', ['hatari-ui.desktop']),
      ]
)
