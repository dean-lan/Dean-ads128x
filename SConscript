from building import *

cwd = GetCurrentDir()
src = []
path = [cwd]

if GetDepend(['PKG_USING_ADS128X']):
    # bare driver core (always)
    src += Glob('src/ads128x_core.c')
    path += [cwd + '/include', cwd + '/src']
    # optional RT-Thread ADC device wrapper
    if GetDepend(['ADS128X_USING_ADC_DEVICE']):
        src += Glob('src/ads128x_adc.c')
    # optional standard acquisition-device class (acqN, unified RT_ACQ_CTRL)
    if GetDepend(['ADS128X_USING_ACQDEV']):
        src += Glob('src/acq_device.c')
        src += Glob('src/ads128x_acqdev.c')
    # optional DeanDAQ acquisition module (multi-chip batch publish)
    if GetDepend(['ADS128X_USING_ACQ']):
        src += Glob('src/ads128x_acq.c')
    # sample
    if GetDepend(['ADS128X_SAMPLE']):
        src += Glob('examples/*.c')

group = DefineGroup('ADS128X', src, depend=['PKG_USING_ADS128X'], CPPPATH=path)

Return('group')
