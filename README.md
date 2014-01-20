Stockfish
=========

Stand alone chess computer with Stockfish and DGT Board

This branch supports a pure python implementation with the pyfish engine (taken from the pyfish branch).

It also supports the piface.

To run on the piface and desktop:

1. Go to the py/ folder
1. Do this one time: https://github.com/piface/pifacecad
1. Also do this one time: "sudo pip install -r requirements.txt" 
1. Execute "python pycochess.py"
1. Fixed time modes should work now (with occasional issues).


To test the DGT driver:

1. Go to the py/ folder
2. Execute "python pydgt.py <device name>" such as /dev/ttyUSB0
3. Ensure that moving a piece on the board will return a new FEN and board graphic
