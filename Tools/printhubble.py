"""
@author: phdenzel

Use glass to read glass state files and print statstics on the modelled Hubble constant
"""
from __future__ import division
# import sys
import numpy as np

glass_basis('glass.basis.pixels', solver=None)
exclude_all_priors()
opts = Environment.global_opts['argv']
gls = [loadstate(f) for f in opts[1:]]


@command
def hubble_stats(env, **kwargs):
    """
    Get Hubble constant statistics from the model distriubtion, i.e.
    the median, the 32nd, and 68th percentile values

    Args:
        env <glass.Environment object> - loaded glass state environment

    Kwargs:
        models <list(glass.model)> - glass input models, other than from the environment
        obj_index <int> - lens model index, in case more than one objects were modelled
        key <str> - model selector key
        sigma <str> - percentile range for statistics; '1sigma', '2sigma', '3sigma', or 'all'
        percentile <bool> - if False, std. dev. high/lows are returned, instead of percentiles

    Return:
        h, high, low <float,float,float> - Hubble constant median, high, and low percentile value
    """
    models = kwargs.pop('models', env.models)
    obj_index = kwargs.pop('obj_index', 0)
    key = kwargs.pop('key', 'accepted')
    sigma = kwargs.pop('sigma', '1sigma')
    frac = {'1sigma': 0.6827,
            '2sigma': 0.9545,
            '3sigma': 0.9973,
            'all': 1.0}.get(sigma, sigma)
    percentile = kwargs.pop('percentile', True)

    cross_model_data = [[], [], []]  # 0: not_accepted; 1: accepted; 2: notag
    for m in models:
        obj, data = m['obj,data'][obj_index]
        if data and 'H0' in data:
            # False = 0; True = 1; default = 2
            cross_model_data[m.get(key, 2)].append(data['H0'])

    h_data = cross_model_data[True]
    if len(h_data) > 0:
        h = np.median(h_data)
        if percentile:
            hhi = np.percentile(h_data, (0.5+frac/2)*100, interpolation='higher')
            hlo = np.percentile(h_data, (1-(0.5+frac/2))*100, interpolation='lower')
        else:
            std = np.std(h_data)
            hhi = h + std
            hlo = h - std
        return h, hhi, hlo
    else:
        return -99, -99, 99


for g in gls:
    h, h68, h32 = g.hubble_stats(percentile=True)
    # print "{0:.2f} {1:.2f} {2:.2f}".format(h, h68, h32)
    if isinstance(h, float):
        herr = (h68-h32)/2
        print "{0:.2f} \t{1:.2f}".format(h, herr)
    else:
        herr = (h68-h32)//2
        print "{0:} \t{1:}".format(h, herr)
