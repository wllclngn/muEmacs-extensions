package main

// DFSMode determines the traversal complexity level
type DFSMode int

const (
    ModeSimple DFSMode = iota  // Classic semaphore pattern
    ModeAdvanced               // Advanced scheduler
    ModeAuto                   // Automatic mode selection
)

// TraversalStrategy determines traversal order (legacy/compat)
type TraversalStrategy int

const (
    StrategyDepthFirst TraversalStrategy = iota
    StrategyBreadthFirst
    StrategyRandom
    StrategyWorkStealing
    StrategyAdaptive
)

func strategyName(s TraversalStrategy) string {
    names := []string{"DepthFirst", "BreadthFirst", "Random", "WorkStealing", "Adaptive"}
    if int(s) < len(names) {
        return names[s]
    }
    return "Unknown"
}

func modeName(m DFSMode) string {
    names := []string{"Simple", "Advanced", "Auto"}
    if int(m) < len(names) {
        return names[m]
    }
    return "Unknown"
}

