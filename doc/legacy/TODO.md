# XPilot Legacy Feature Ideas (TODO)

> [!NOTE]
> Ported from `legacy/xpilot-ng-4.7.3/doc/TODO`. Historical design notes and ideas contributed by Claus Leth Gregersen and Mark Boyns.
> 
> [← Back to Documentation Index](../OLD_DOCS.md)

## Team Game Concept: Conquest & Shared Lives

### Proposal by Claus Leth Gregersen

> "New sort of teamgame:
> I don't know if you ever played Z but the idea is to add some areas on the map you have to conquer. Which you do by flying over it (maybe the diamonds used as checkpoints on race maps could be reused).
> Each team has a total number of lives, not split out on the individuals, so they share them between them. Each area will every 30 second or so produce an extra life to the team who possesses it. Winner is the team who survives longest."

### Feedback by Mark Boyns

> "Even pieces of this are good ideas. Shared team lives could make team games more interesting. Conquering areas could be neat if there were 4 such areas and 4 players per team. The first team to conquer all 4 at the same time wins."

### Concept Map Layout & Laser Fences

```text
##############################################################################
##                         cccc             |  |  |                         ##
##                                          |  |  |                     1   ##
##                                          |  |  |                  *      ##
##                     v   cccc             |  |  |                         ##
##      ######################################################################
##                                          |                               ##
##                          *               |                             <-##
##->                                        |                               ##
##                                          |                               ##
######################################################################      ##
##                                          |                               ##
##->                        *               |                               ##
##                                          |                             <-##
##                                          |                               ##
##        ####################################################################
##                         CCCC             |  |  |                         ##
##                                          |  |  |                    *    ##
##                                          |  |  |                     2   ##
##                     V   CCCC             |  |  |                         ##
##############################################################################
```

**Legend:**
- `1` and `2`: Team spawn base locations
- `#`: Solid walls
- `*`: Areas / checkpoints to conquer
- `|`: Laser fences — stationary beam that fires continuously for 5–10 frames, recharges, and fires again.
