from __future__ import division
import os

glass_basis('glass.basis.pixels', solver=None)
exclude_all_priors()
opts = Environment.global_opts['argv']
gls = [loadstate(f) for f in opts[1:]]


if 1:
    import pylab as pl
    from pylab import show, figure, ion, ioff, savefig, gcf, close
if 0:
    import matplotlib as mpl
    mpl.use('Agg')
    import matplotlib.pyplot as pl
    from matplotlib.pyplot import figure, ion, ioff, savefig, gcf, close


def escape(s):
    s = s.replace('_', r'\_')
    return s


def style_iterator(colors='gbrcm'):
    import matplotlib.lines as mpll
    from itertools import count
    _linestyles = [k for k, v, in mpll.lineStyles.iteritems() if not v.endswith('nothing')]
    _linestyles.sort()
    for lw in count(1):
        for ls in _linestyles:
            for clr in colors:
                yield lw, ls, clr


Lscale = 2
Mscale = 1.8e10
Rcut = 50

fig_plot_size = None  # NxN inches
fig_nr, fig_nc = None, None
fig_subplot_index = None
produce_subfiles = None


def init_plots(size, dim, with_subfiles=False):
    global fig_plot_size, fig_nr, fig_nc, fig_subplot_index, produce_subfiles
    fig_plot_size = size
    fig_nr, fig_nc = dim
    fig_subplot_index = 1
    produce_subfiles = with_subfiles
    f = figure(figsize=(fig_plot_size*fig_nc, fig_plot_size*fig_nr))


def begin_plot():
    global fig_subplot_index
    if not produce_subfiles:
        gcf().add_subplot(fig_nr, fig_nc, fig_subplot_index)
    fig_subplot_index += 1


def end_plot():
    if produce_subfiles:
        tag = chr(ord('a') + (fig_subplot_index-1))
        savefig('%s%s.png' % (os.path.splitext(state_file))[0], tag)


def PlotFigure(g, title=None):
    g.make_ensemble_average()
    g.bw_styles = True

    init_plots(4, [2, 4])
    gcf().subplots_adjust(left=0.05, right=0.98)
    if title is not None:
        gcf().suptitle(title)

    if 1:
        begin_plot()
        # g.img_plot(obj_index=0)
        g.img_plot(obj_index=0, color='#fe4365')
        g.arrival_plot(g.ensemble_average, obj_index=0, only_contours=True,
                       clevels=75, colors=['#603dd0'])
        # g.arrival_plot(g.ensemble_average, obj_index=1, only_contours=True,
        #                clevels=50, colors=['#da9605'])
        # g.src_plot(obj_index=0)
        # g.src_plot(g.ensemble_average, obj_index=0)
        # g.external_mass_plot(0)
        end_plot()

    if 1:
        begin_plot()
        # g.H0inv_plot(color='#603dd0')
        g.H0inv_plot()
        end_plot()

    if 1:
        begin_plot()
        g.H0_plot()
        end_plot()

    if 1:
        begin_plot()
        si = style_iterator(colors)
        lw, ls, clr = si.next()
        g.glerrorplot('kappa(R)', ['R', 'arcsec'], yscale='linear')
        end_plot()

    if 1:
        begin_plot()
        g.kappa_plot(g.ensemble_average, 0, with_contours=True, clevels=20)
        # , vmax=1, colors='r')
        end_plot()

    if 1:
        begin_plot()
        g.shear_plot2d()
        end_plot()

    if 0:
        begin_plot()
        g.time_delays_plot()
        end_plot()


# ------------------------------------------------------------------------------

if len(gls) == 1:
    colors = 'k'
else:
    colors = 'rgbcm'

for i, g in enumerate(gls):
    ion()
    fname = os.path.basename(g.global_opts['argv'][i+1])
    hname = "".join(fname.split('.')[:-1] + ['_vs.pdf'])
    PlotFigure(g, title="".join(fname.split('.')[:-1]))
    ioff()
    savefig(hname)
    close()
