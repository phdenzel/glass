from __future__ import division
import sys
from numpy import amin, amax, diff, argsort, abs, array, sum, mat, eye, asarray, matrix
from scales import time_to_physical
from scipy.ndimage.filters import correlate1d
from scipy.misc import central_diff_weights

from pylab import contour, matshow, show, over

def raytrace(obj, ps, sys_index, nimgs=None, eps=None):

    #---------------------------------------------------------------------------
    # Find the derivative of the arrival time surface.
    #---------------------------------------------------------------------------
    arrival = obj.basis.arrival_grid(ps)[sys_index]

    w = central_diff_weights(3)
    d = abs(correlate1d(arrival, w, axis=0, mode='constant')) \
      + abs(correlate1d(arrival, w, axis=1, mode='constant'))
    d = d[1:-1,1:-1]

    matshow(d)

    xy = obj.basis.refined_xy_grid(ps)[1:-1,1:-1]

    # Create flattened *views*
    xy     = xy.ravel()
    dravel = d.ravel()

    imgs = []
    offs = []
    print 'searching...'
    for i in argsort(dravel):

        if nimgs == len(imgs): break

        for img in imgs:
            if abs(img-xy[i]) <= eps: break
        else:
            imgs.append(xy[i])
            offs.append(i)

    #---------------------------------------------------------------------------
    # Print the output
    #---------------------------------------------------------------------------

    if imgs:
        #print imgs
        #if len(imgs) % 2 == 1: imgs = imgs[:-1]  # XXX: Correct?
        imgs = array(imgs)

        g0 = array(arrival[1:-1,1:-1], copy=True)
        g0ravel = g0.ravel()
        times = g0ravel[offs]
        order = argsort(times)
    else:
        order = []

    return [(times[i], imgs[i]) for i in order]

def write_code(obj, ps, sys_index, seq):

    if not seq: return

    letters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'
    for [t,img], l in zip(seq, letters):
        sys.stdout.write('%s = %-.4f, %-.4f\n' % (l, img.real, img.imag))

    sys.stdout.write("source(%.2f,A,'min'" % obj.sources[sys_index].z)
    prev = seq[0][0]
    for [t,img],l in zip(seq[1:], letters[1:]):
        t0 = time_to_physical(obj, t-prev) * ps['1/H0'] * obj.basis.top_level_cell_size**2
        if t0 < 1e-4:
            sys.stdout.write(", %s,'',%.4e" % (l, t0))
        else:
            sys.stdout.write(", %s,'',%.4f" % (l, t0))
        prev = t
    sys.stdout.write(')\n')

