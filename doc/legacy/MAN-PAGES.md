# XPilot Legacy Man Pages Guide (man/README)

> [!NOTE]
> Ported from `legacy/xpilot-ng-4.7.3/doc/man/README`.
> 
> [← Back to Documentation Index](../OLD_DOCS.md)

## Reading Man Pages Locally

To read the raw roff man pages without installing them system-wide, run:

```bash
nroff -man <filename> | more
```

For example:
```bash
nroff -man legacy/xpilot-ng-4.7.3/doc/man/xpilot-ng-server.man | more
nroff -man legacy/xpilot-ng-4.7.3/doc/man/xpilot-ng-sdl.man | more
```

## Generation

Man pages in `legacy/xpilot-ng-4.7.3/doc/man/` were originally generated using `help2man` via the helper script `help2man.sh`.
