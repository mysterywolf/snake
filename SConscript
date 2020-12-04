from building import *

group = []
if not GetDepend(['PKG_USING_SNAKE']):
    Return('group')

src	= Glob('*.c')

group = DefineGroup('snake', src, depend = ['PKG_USING_SNAKE'])

Return('group')
