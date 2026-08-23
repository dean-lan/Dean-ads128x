from building import *

cwd = GetCurrentDir()
src = []
path = [cwd]

if GetDepend(['PKG_USING_ADS128X']):
    # ADS128x chip driver files (each guards itself with #if, e.g. ADC_DEVICE/ACQDEV/ACQ)
    src += Glob('src/ads128x/*.c')
    path += [cwd + '/include', cwd + '/include/ads128x', cwd + '/src/ads128x']
    # shared acquisition-device class (acq_device.h/.c)
    if GetDepend(['ADS128X_USING_ACQDEV']):
        src += Glob('src/acq_device.c')
    # sample
    if GetDepend(['ADS128X_SAMPLE']):
        src += Glob('examples/*.c')

group = DefineGroup('ADS128X', src, depend=['PKG_USING_ADS128X'], CPPPATH=path)

Return('group')
