# XPilot Legacy Known Bugs (BUGS)

> [!NOTE]
> Ported from `legacy/xpilot-ng-4.7.3/doc/BUGS`. Tracks historical unresolved bugs in early XPilot versions.
> 
> [← Back to Documentation Index](../OLD_DOCS.md)

## Tracking Format Specification

```text
$Id: BUGS,v 1.1.1.1 2000/11/01 11:24:59 uau Exp $

#
# Each bug has its unique number and some additional info.
# The <BUG> field has the number (this marks the start of the bug entry).
# The <STATUS> field can be: open|fixed|unconfirmed|deferred
# Additionally, one may have an extra /-separated priority modifier
# (low|medium|high), for example: <STATUS>open/medium.
# The <VER> field is the version the bug was reported against.
# The <DESC> field holds the person (+email) who reported the bug
# plus the description of the bug.
# The <WORK> field reports progress made on fixing it
# (same format as <DESC>).
#
# The six digit dates used here are in yymmdd format for easy sorting.
#
# When refering to bugs one may use B+<the bug number>,
# i.e. B001 refers to the ball-string bug.
#
```

## Bug Entries

### Bug 002

- **Status:** `open/low`
- **Reported against version:** `2.0`
- **Description:**
  - *bjoerns*

    ```text
    Ball should be affected by object collisions.
    ```

---

