

class ShockTubeProblem:
    
    def __init__(self, L={ }, R={ }, gamma=1.4):

        self._setup()

        self.L_state.update(L)
        self.R_state.update(R)
        self.adiabatic_gamma = gamma


class SRShockTube1(ShockTubeProblem):

    """
    This is Problem 1 of Marti & Muller's article, except that the
    pressure of the left state has been set apart from zero.

    See: http://relativity.livingreviews.org/Articles/lrr-2003-7/
    """

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        self.L_state = { 'Rho':10.0, 'Pre':13.33, 'v': [0,0,0], 'B': [0,0,0] }
        self.R_state = { 'Rho': 1.0, 'Pre': 0.01, 'v': [0,0,0], 'B': [0,0,0] }


class SRShockTube2(ShockTubeProblem):

    """
    This is Problem 2 of Marti & Muller's article. It is very
    challenging and still fails many modern SR codes.

    See: http://relativity.livingreviews.org/Articles/lrr-2003-7/
    """

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        self.L_state = { 'Rho': 1.0, 'Pre':1000.00, 'v': [0,0,0], 'B': [0,0,0] }
        self.R_state = { 'Rho': 1.0, 'Pre':   0.01, 'v': [0,0,0], 'B': [0,0,0] }


class RMHDShockTube1(ShockTubeProblem):

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        self.L_state = { 'Rho': 1.000, 'Pre':1.0, 'v': [0,0,0], 'B': [0.5, 1.0, 0.0] }
        self.R_state = { 'Rho': 0.125, 'Pre':0.1, 'v': [0,0,0], 'B': [0.5,-1.0, 0.0] }


class RMHDShockTube2:

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        self.L_state = { 'Rho': 1.08, 'Pre': 0.95, 'v': [ 0.40, 0.3, 0.2], 'B': [2.0, 0.3, 0.3] }
        self.R_state = { 'Rho': 0.95, 'Pre': 1.00, 'v': [-0.45,-0.2, 0.2], 'B': [2.0,-0.7, 0.5] }


class RMHDShockTube3(ShockTubeProblem):

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        self.L_state = { 'Rho': 1.00, 'Pre': 0.1, 'v': [ 0.999, 0.0, 0.0], 'B': [10.0, 7.0, 7.0] }
        self.R_state = { 'Rho': 1.00, 'Pre': 0.1, 'v': [-0.999, 0.0, 0.0], 'B': [10.0,-7.0,-7.0] }


class RMHDShockTube4(ShockTubeProblem):

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        self.L_state = { 'Rho': 1.0, 'Pre': 5.0, 'v': [0.0, 0.3, 0.4], 'B': [1.0, 6.0, 2.0] }
        self.R_state = { 'Rho': 0.9, 'Pre': 5.3, 'v': [0.0, 0.0, 0.0], 'B': [1.0, 5.0, 2.0] }


class RMHDContactWave(ShockTubeProblem):

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        self.L_state = { 'Rho': 1.0, 'Pre': 1.0, 'v': [0.0, 0.7, 0.2], 'B': [5.0, 1.0, 0.5] }
        self.R_state = { 'Rho': 0.1, 'Pre': 1.0, 'v': [0.0, 0.7, 0.2], 'B': [5.0, 1.0, 0.5] }


class RMHDRotationalWave(ShockTubeProblem):

    def __init__(self, L={ }, R={ }, gamma=1.4):

        ShockTubeProblem.__init__(self, L=L, R=R, gamma=gamma)

    def _setup(self):

        vR = [0.400000,-0.300000, 0.500000]
        vL = [0.377347,-0.482389, 0.424190]

        self.L_state = { 'Rho': 1.0, 'Pre': 1.0, 'v': vL, 'B': [2.4, 1.0,-1.600000] }
        self.R_state = { 'Rho': 1.0, 'Pre': 1.0, 'v': vR, 'B': [2.4,-0.1,-2.178213] }
