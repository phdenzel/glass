from scales import convert
from numpy import cumsum, mean, average, array, where
from scipy.special import gamma as Gamma
from environment import DArray

def estimated_Re(obj, ps, src_index):

    #---------------------------------------------------------------------
    # Estimate an Einstein radius. 
    # Take the inertia tensor of the pixels above kappa_crit and use the
    # eigenvalues to scale the most distance pixel position to the major
    # and minor axes. Re is then defined here as the mean of the two.
    #
    # TODO: On convergence. Since the centers of each pixel are used, as
    # the resolution increases, Re will tend to move outward. A better
    # solution would be to use the maximum extent of each pixel.
    #---------------------------------------------------------------------

    kappa = ps['enckappa'] / obj.sources[src_index].zcap / cumsum(map(len,obj.basis.rings))

    #print map(len,obj.basis.rings)

    #print '^' * 10
    #print kappa
    #print '^' * 10
    w = kappa >= 1.0

    #print w

    if not w.any(): return 0,0,0,0

    w = where(w)[0][-1]

    #print w

    r = obj.basis.ploc[obj.basis.rings[w][0]]

    #print r

    Vl = abs(r)
    Vs = abs(r)
    if Vl < Vs: 
        Vl,Vs = Vs,Vl
        D1,D2 = D1,D2

    return mean([Vl,Vs]), Vl, Vs, 0

def estimated_profile_slope(m, vdisp_true, beta):

    a = r_half * (2**(1./(3-beta)) - 1)

    def f(gamma):
        return (a/2)**(2-gamma) * Gamma(gamma)**2 * Gamma(5 - gamma - beta) / Gamma(3 - beta)

def default_post_process(m):
    obj,ps = m
    b = obj.basis

    rscale = convert('arcsec to kpc', 1, obj.dL, ps['nu'])

    dscale1 = convert('kappa to Msun/arcsec^2', 1, obj.dL, ps['nu'])
    dscale2 = convert('kappa to Msun/kpc^2',    1, obj.dL, ps['nu'])

    #ps['R']     = b.rs + b.radial_cell_size / 2
    #ps['R_kpc'] = ps['R'] * rscale

    ps['R'] = {}
    ps['R']['arcsec'] = b.rs + b.radial_cell_size / 2
    ps['R']['kpc']    = ps['R']['arcsec'] * rscale

    ps['enckappa']   = cumsum([    sum(ps['kappa'][r]                  )         for r in b.rings])
    ps['encmass']    = cumsum([    sum(ps['kappa'][r]*b.cell_size[r]**2)*dscale1 for r in b.rings])
    ps['sigma']      =  array([average(ps['kappa'][r]                  )*dscale2 for r in b.rings])
    ps['kappa prof'] =  array([average(ps['kappa'][r]                  )         for r in b.rings])

    ps['Re'] = {}
    ps['Re']['arcsec'] = [ estimated_Re(obj, ps,i)[0] for i,src in enumerate(obj.sources) ]
    ps['Re']['kpc']    = [ r * rscale              for r in ps['Re']['arcsec'] ]

    #Mtot = sum(ps['kappa'])
    #ps['R half mass'] = ps['R'] 
    #kappa = ps['enckappa'] / obj.sources[src_index].zcap / cumsum(map(len,obj.basis.rings))

    # convert to DArray

    ps['R']['arcsec'] = DArray(ps['R']['arcsec'], ul=['arcsec', r'$R$ $(\mathrm{arcsec})$'])
    ps['R']['kpc']    = DArray(ps['R']['kpc'],    ul=['kpc',    r'$R$ $(\mathrm{kpc})$'])

    ps['enckappa']   = DArray(ps['enckappa'],   ul=['kappa',      r'$\kappa(<R)$'])
    ps['encmass']    = DArray(ps['encmass'],    ul=['Msun',       r'$M(<R)$ $(M_\odot)$'])
    ps['sigma']      = DArray(ps['sigma'],      ul=['Msun/kpc^2', r'$\Sigma$ $(M_\odot/\mathrm{kpc}^2)$'])
    ps['kappa prof'] = DArray(ps['kappa prof'], ul=['kappa',      r'$\langle\kappa(R)\rangle$'])

    ps['Re']['arcsec'] = [ DArray(v, ul=['arcsec', r'$R$ $(\mathrm{arcsec})$']) for v in ps['Re']['arcsec'] ]
    ps['Re']['kpc']    = [ DArray(v, ul=['kpc',    r'$R$ $(\mathrm{kpc})$'   ]) for v in ps['Re']['kpc']    ]
