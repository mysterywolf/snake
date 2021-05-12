Import('rtconfig')
from building import *

src	= Glob('*.c')
group = DefineGroup('snake', src, depend = ['PKG_USING_SNAKE'])
Return('group')
