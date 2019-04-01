"""
@author: phdenzel

Use glass to read glass state files and print statstics on the modelled Hubble constant
"""
from __future__ import division
# import sys
import os
import numpy as np
from matplotlib import pyplot as plt

glass_basis('glass.basis.pixels', solver=None)
exclude_all_priors()
opts = Environment.global_opts['argv']
gls = [loadstate(f) for f in opts[1:]]


@command
def hubble_dist(env, **kwargs):
    """
    Get Hubble constant statistics from the model distriubtion, i.e.
    the median, the 32nd, and 68th percentile values

    Args:
        env <glass.Environment object> - loaded glass state environment

    Kwargs:
        models <list(glass.model)> - glass input models, other than from the environment
        obj_index <int> - lens model index, in case more than one objects were modelled
        key <str> - model selector key

    Return:
        h, high, low <float,float,float> - Hubble constant median, high, and low percentile value
    """
    models = kwargs.pop('models', env.models)
    obj_index = kwargs.pop('obj_index', 0)
    key = kwargs.pop('key', 'accepted')

    cross_model_data = [[], [], []]  # 0: not_accepted; 1: accepted; 2: notag
    for m in models:
        obj, data = m['obj,data'][obj_index]
        if data and 'H0' in data:
            # False = 0; True = 1; default = 2
            cross_model_data[m.get(key, 2)].append(data['H0'])

    h_data = cross_model_data[True]
    return h_data


for i, g in enumerate(gls):
    fname = os.path.basename(g.global_opts['argv'][1+i])
    h_data = g.hubble_dist()
    plt.hist(h_data, range=[50, 90], bins=20)
    plt.title(fname)
    hname = "".join(fname.split('.')[:-1] + ['_hist.png'])
    plt.savefig(hname)
    plt.close()
