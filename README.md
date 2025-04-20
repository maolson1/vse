VSE (Very Safe Executor) is a general-purpose Windows service wrapper for cmd commands. The input command is run in a new Local System service, after an optional delay. After execution, the service deletes itself.

```
vse -cmd <command> [-delay <seconds>]
```

VSE was mainly created as a less-frustrating alternative to the Task Scheduler for running detached programs on Windows hosts over SSH.
