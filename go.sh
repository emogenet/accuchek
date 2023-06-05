#!/bin/bash

sudo -s << EOF
  ./accuchek >z.json
  chown -R mgix.mgix .
  mv z.json ~mgix/finance/glucose
  chown -R mgix.mgix ~mgix/finance/glucose
  cd ~mgix/finance/glucose
  /usr/bin/python3 ./upload_glucose.py
EOF

