# The reason i'm having this file to just link to some folder, is that I have to work with 2 instances of Typerminal(Debug and Shipment builds).
#  And I want their workspaces to be the same.

import sys
import os
import platform
from pathlib import Path

pc_name = platform.node()

if pc_name == 'DESKTOP-EIUG5MQ':
    sys.path.append("E:/typerminal_workspace")
elif pc_name == 'fridge':
	os.environ['PATH'] += os.pathsep + '/home/peppingdore/.cargo/bin'
	sys.path.append(os.path.join(Path.home(), "Typerminal_Workspace"))

del(pc_name)

from b_main import *