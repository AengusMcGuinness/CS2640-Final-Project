# AI Usage Report

## Overview

I used AI assistance throughout this project as a brainstorming partner,
debugging assistant, pair programmer, data-analysis helper, and writing
reviewer.  The project still required me to choose the research question,
understand the RDMA programming model, run the CloudLab experiments, debug the
cluster environment, interpret the results, and decide what conclusions were
fair to claim.  AI was most useful when I treated it as a technical collaborator
that could speed up iteration, but whose output needed to be checked against
the code, the experimental data, and the actual behavior of the cluster.

## How I Used AI

### Project Planning and Scope

I used AI early in the project to help refine the original idea into a concrete
set of goals:

- a TCP/RPC key-value store baseline,
- a two-sided RDMA implementation,
- a one-sided RDMA read path, and
- a metadata-overhead experiment using RDMA atomics.

This helped turn a broad idea about RDMA caching into a project with clear
75%, 100%, and 125% milestones.  AI was especially helpful for identifying what
would be feasible in a course project and what would be too large, such as a
complete production-quality distributed LRU protocol.

### Coding Assistance

I used AI as a pair-programming assistant for several parts of the codebase,
including:

- the TCP key-value server and benchmark harness,
- the RDMA server and client paths,
- the one-sided RDMA slot layout,
- the metadata atomic benchmark mode,
- experiment runner scripts,
- CPU and network metric collection scripts,
- plotting and aggregation scripts, and
- Doxygen configuration and source-code documentation.

The AI helped draft code, explain existing code, find likely causes of build or
runtime errors, and make repetitive script changes faster.  I still had to run
the code, inspect compiler errors, debug CloudLab-specific RDMA behavior, and
decide whether the implementation matched the intended experiment.

### Debugging and Experiment Execution

AI was useful for diagnosing problems that came up while running on CloudLab.
Examples included:

- distinguishing two-sided and one-sided RDMA server modes,
- fixing build and link errors after code changes,
- choosing the correct RDMA device for the private experiment network,
- understanding why SSH-based metric collection did not work in my setup,
- switching to direct server-side metric collection, and
- identifying why Linux netdev byte counters were not valid for RDMA traffic.

One important lesson was that AI suggestions about cluster commands often need
to be adapted to the exact environment.  For example, the public CloudLab host
names and the private experiment network were not interchangeable, and the RDMA
device used for the private network was `mlx5_3`, not the initially assumed
`mlx5_0`.

### Data Analysis and Plotting

I used AI to help create scripts that aggregate multiple trials, generate plots
with error bars, and validate the final CSV files.  AI helped check that the
final data was internally consistent:

- all three final trials were present,
- each transport had measurements for 1, 2, 4, and 8 clients,
- all warmup and measured error counts were zero, and
- each final summary point included three trials.

AI also helped identify an invalid measurement: the network byte counters
reported plausible values for TCP but near-zero bytes per operation for RDMA.
Since that is physically impossible for RDMA reads and messages, I treated the
network-byte results as inconclusive instead of using them as evidence.

### Report and Presentation

I used AI to help organize the final report, summarize the results, revise the
writing, and make sure the conclusions matched the data.  AI also helped
generate and revise presentation material.  I used it to improve clarity, but I
tried to keep the final argument grounded in the actual measurements rather
than overstating the results.

## What I Learned

The most useful thing I learned from using AI is that it can accelerate systems
work, but it does not replace systems understanding.  For example, AI could
explain the expected RDMA queue-pair setup sequence, but I still needed to
understand why a queue pair failed to transition, which network interface was
being used, and whether a counter was measuring the right traffic.

I also learned that AI is good at creating automation around experiments.  The
runner scripts, aggregation scripts, and plotting workflow made it much easier
to repeat experiments and avoid manual copy-paste mistakes.  This was one of
the biggest practical benefits of AI assistance.

Finally, I learned that AI can make writing faster, but the important part of
technical writing is judgment.  The final report needed to distinguish between
strong results, noisy measurements, and invalid measurements.  AI helped draft
language, but I had to decide what claims were defensible.

## What Surprised Me

I was surprised by how much time went into experiment infrastructure compared
with the initial implementation.  Getting RDMA to run, choosing the correct
device, collecting CPU measurements, repeating trials, and validating plots
were all more involved than simply writing the benchmark code.

I was also surprised by how easy it is for a plausible measurement to be wrong.
The network-byte counter experiment looked useful at first, but after checking
the values it became clear that the Linux netdev counters were not observing
the RDMA data path in a meaningful way.  This reinforced the importance of
sanity-checking every metric.

## Tips for Future Students

- Use AI to create repeatable scripts early.  Manual experiment commands become
  hard to manage once there are multiple transports, workloads, and trials.
- Ask AI to explain errors, but verify the explanation against the actual
  system state.
- Keep raw CSVs and regenerated plots under versioned experiment directories so
  it is clear which data produced which figures.
- Treat AI-generated performance claims skeptically until they are supported by
  measurements.
- Be honest about failed or inconclusive measurements.  Understanding why a
  metric is invalid can be as valuable as reporting a successful result.

## Limitations of AI Assistance

AI could not directly run the CloudLab RDMA experiments for me, inspect the
remote hardware unless I provided command output, or know which network device
was correct without evidence from the cluster.  It also sometimes suggested
commands that needed to be adjusted for my environment.  Because of that, I
used AI-generated code and commands as starting points, then validated them by
building, running tests, collecting data, and checking the resulting CSVs and
plots.

Overall, AI made the project faster and more organized, but the main project
work still came from implementing the system, running the experiments,
debugging the environment, and interpreting the results carefully.
