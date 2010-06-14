#!/usr/bin/env python


def run_problem(solver, problem, domain, name=None, quiet=True, CFL=0.1, tfinal=0.2):
    from time import time
    from hydro.boundary import OutflowBoundary

    if name is None: name = problem.__class__

    solver.new_problem(domain)

    Nq = solver.NumComponents
    Ng = solver.NumGhostCells
    P  = problem.initial_model(domain, Ng, Nq)

    t  = 0.0
    dt = 1e-9
    nc = 0

    Nx = P.shape[0]
    boundary = OutflowBoundary()
    min_dx = min(domain.dx)

    start_time = time()
    while t < tfinal:

        nc += 1
        start = time()

        domain.set_BC(P, Ng, BC=None)
        solver.advance_state(P,dt)

        t += dt
        step_time = time()-start

        msg_data = (nc, t, dt, 1e6*step_time/P.size, 0)
        msg_text = "N: %05d t: %6.4f dt: %6.4e us/zone: %5.4f failures: %d"
        if not quiet: print msg_text % msg_data

        dt = CFL * min_dx

    print "Python driver finished '%s'... total time: %f" % (name, time() - start_time)
    return P[Ng:-Ng]


def consolidate():
    from os import listdir
    from pickle import load

    fnames = [x for x in listdir('.') if x.endswith('.pk')]
    arrays = [load(open(fn)) for fn in sorted(fnames)]
    Nx = sum(a.shape[0] for a in arrays)
    P = zeros((Nx,1))

    for i,a in enumerate(arrays):
        nx = a.shape[0]
        P[i*nx:(i+1)*nx,:] = a

    return P


if __name__ == "__main__":
    from hydro import *
    from numpy import array, zeros
    from hydro.testbench import *
    from hydro import visual
    from hydro.parallel import DecomposedDomain

    solver = ScalarEquationsSolver(scheme='fwd_euler')

    domain = DecomposedDomain(N=(256,))
    problem = SRShockTube1()

    P = run_problem(solver, problem, domain, quiet=True, CFL=0.4, tfinal=0.1)
    domain.dump(P)

    from mpi4py.MPI import COMM_WORLD

    if COMM_WORLD.rank is 0:

        P = consolidate()

        from pylab import plot, show, legend
        visual.shocktube(P)
        show()
