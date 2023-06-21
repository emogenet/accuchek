#!/bin/bash

set -x
#sudo -s << EOF
  set -x
  ./accuchek >z.json
  #chown -R mgix.mgix .
  mv z.json ~mgix/finance/glucose
  #chown -R mgix.mgix ~mgix/finance/glucose
  cd ~mgix/finance/glucose
  /usr/bin/python3 ./upload_glucose.py
#EOF

