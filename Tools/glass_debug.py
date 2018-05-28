from __future__ import division

glass_basis('glass.basis.pixels', solver=None)
exclude_all_priors()
opts = Environment.global_opts['argv']
gls = [loadstate(f) for f in opts[1:]]


@command
def gls_info(env, **kwargs):
    models = env.models
    obj_index = 0
    print "Model"
    print type(models)
    print type(models[0])
    print dir(models[0])
    print models[0].keys()
    print
    print "OBJ,data"
    print len(models[0]['obj,data'][obj_index])
    print type(models[0]['obj,data'][obj_index][0])
    print dir(models[0]['obj,data'][obj_index][0])
    print
    print "obj,DATA"
    print len(models[0]['obj,data'][obj_index])
    print type(models[0]['obj,data'][obj_index][1])
    print dir(models[0]['obj,data'][obj_index][1])
    print
    print "obj,SOL"
    print len(models[0]['obj,sol'][obj_index])
    print type(models[0]['obj,sol'][obj_index][1])
    print
    print "Arrival grid"
    print models[0]['obj,data'][obj_index][0].basis.arrival_grid(
        models[0]['obj,data'][obj_index][1])[obj_index]


for g in gls:
    g.gls_info()
