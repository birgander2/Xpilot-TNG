# XPilot Legacy Resolved Bugs (FIXED)

> [!NOTE]
> Ported from `legacy/xpilot-ng-4.7.3/doc/FIXED`. Historical bug tracking archive of bugs resolved during XPilot 1.x through 3.x.
> 
> [← Back to Documentation Index](../OLD_DOCS.md)

## Bug Entries

### Bug 003

- **Status:** `fixed`
- **Reported against version:** `1.2`
- **Description:**
  - *unknown*

    ```text
    Players can fly through walls at high speed, this isn't the case for
	objects (they use different collision detection methods).
    ```

---

### Bug 004

- **Status:** `fixed/low`
- **Reported against version:** `3.0.4`
- **Description:**
  - *930900: kenrsc@stud.cs.uit.no*

    ```text
    Limited lives.  New human players are dead when entering a game
	where somebody has lost a life. This is not true for robots.
    ```
- **Work Log:**
  - *930915 kenrsc@stud.cs.uit.no*

    ```text
    Bert did you not change it so that if robots where left in the game
	it would start over again ? If so, does it do anything harm having
	the robots entering. Lowered the status to low since robots wont
	join in to often.
    ```
  - *930919: bert*

    ```text
    I don't expect that adding more robots will be a problem.
	If only robots are alive then they have won the game and the
	game resets.
    ```

---

### Bug 005

- **Status:** `fixed/low`
- **Reported against version:** `2.0`
- **Description:**
  - *930900: kenrsc@stud.cs.uit.no*

    ```text
    The treasure is not correct. Sometimes the ball disapears for
	good.  There are also other things, but I don't remember now.
    ```

---

### Bug 006

- **Status:** `fixed/low`
- **Reported against version:** `3.0.4`
- **Description:**
  - *930910: kenrsc@stud.cs.uit.no*

    ```text
    Mychar for robots 'R' disapears when robots has been dead in
	limited lives.
    ```
- **Work Log:**
  - *930911: bert@mc.bio.uva.nl*

    ```text
    Fixed it I hope.
    ```

---

### Bug 007

- **Status:** `fixed/medium`
- **Reported against version:** `3.0.3`
- **Description:**
  - *930909: alt.games.xpilot*

    ```text
    If you release too many tanks at once the server hangs.
    ```
  - *931001: Derek C. Richardson (dcr@mail.ast.cam.ac.uk)*

    ```text
    Had that tank bug again yesterday: swa launched two tanks directly
	in my face and the server froze with the message:
	   Write socket buffer not big enough (4096,4085,"%c%c%c")
	Has any progress been made with this problem? Has anyone else HAD the
	problem? (we're running SunOS 4.1.x on Sparc IPX's and 10's here).
	Also with tanks: when they're launched they show up in the player list,
	with an amazing negative score (-500 or so). What happens if all the
	bases are occupied so that there is actually no more room for players?
	(this was the case last night). Could this be the origin of the problem?
    ```
- **Work Log:**
  - *930919: bert*

    ```text
    Couldn't reproduce it.  My experiment was to see if it had
	anything to do with not having enough bases.  I don't know if
	tanks need bases at all.
    ```
  - *931001: bert*

    ```text
    Made some small fixes which hopefully solve this problem.
	Tanks weren't properly excluded from certain player calculations.
	Tanks don't need (and didn't use) bases, but this wasn't properly
	reckoned with at all places.
	Clients shouldn't get homebase info for tanks.
    ```

---

### Bug 008

- **Status:** `fixed/low`
- **Reported against version:** `3.0.3`
- **Description:**
  - *930912: chc@dale.ksc.nasa.gov (Charles Curley)*

    ```text
    Has anyone else noticed that on when you get near the edge of a
	wrapped map that you can't shoot when you nose is within say 20
	pixels of the edge?
    ```
- **Work Log:**
  - *930912: bert*

    ```text
    The problem is that when the center of a player is close to the edge
	then the expression "shot->pos = pl->pos + ships[dir].pts[0]" may
	result in a value bigger than the mapwidth/height.
    ```

---

### Bug 009

- **Status:** `fixed`
- **Reported against version:** `3.0.4`
- **Description:**
  - *930920:	kenrsc@stud.cs.uit.no (Ken Ronny Schouten)*

    ```text
    If a player hit a mine you have placed somewhere the point of that
	mine will show up on the position you are now. This is wrong. It 
	should only show up on your hud.
    ```
- **Work Log:**
  - *Fixed the problem. It was a reference to the player position instead*

    ```text
    of the mine position in collision.c
    ```

---

### Bug 010

- **Status:** `fixed`
- **Reported against version:** `3.0.4`
- **Description:**
  - *930923: kenrsc@stud.cs.uit.no (Ken Ronny Schouten)*

    ```text
    If you iconify your window during limited lives it does not always
	pop up again. I think this has something to do with the locking on
	different people. I have never experienced this bug when not locking
	on someone during a game.
    ```
- **Work Log:**
  - *930928: kenrsc@stud.cs.uit.no (Ken Ronny Schouten)*

    ```text
    Fixed it I hope :) The status field for the player that was sent over
        was for the player you where locked on when you where dead and not
	the status field for yourself. Changed this and now it seems to work
	just fine.
    ```

---

### Bug 011

- **Status:** `fixed/medium`
- **Reported against version:** `3.0.4`
- **Description:**
  - *930930: Charles Curley (chc@dale.ksc.nasa.gov)*

    ```text
    Cloaks don't seem to work sometimes.  Mostly with three of more
	people we will have a situation where some players can see
	others all the time regardless of the others cloak situation.
	eric@soda.berkeley.edu (Eric van Bezooijen):
	We here have also noticed the cloak problem.  We are running
	everything on Solaris 2.x on Sparc-boxes.  It is quite rare,
	however.  It is just like he describes.  Once in a while there
	will be a player playing who I can always see, regardless whether
	or not he has cloaking or not, with dashed lines around his ship...
    ```
  - *931007: Jonathan Katz (jonathan@cad.ucla.edu)*

    ```text
    There seems to be a bug in the current release (3.0.5)
	and the last couple) that allows usually a pair of people
	on our server to be unable to cloak from each other.
	The cloaked individual will be 'ghosted' but visible....
	Also they will show up on the world map and radar...
    ```
  - *931011:  Gary O'Brien <gary@hpmtlgo.lvld.hp.com>*

    ```text
    We're using xpillot 3.0.5 on HP 700 series.  We still have the
	cloak bug problem.  We see the problem if there are more than
	2 human players in the game.  The general rule is if your cloaked
	and can see them, then they can see you.  A player which is cloaked
	and not visible cannoot seee you either.
    ```
- **Work Log:**
  - *931001: bert*

    ```text
    Had a thorough look at all of the visibility code and made
	a possible fix.  The updateVisibility flag is now explicitly set
	when a player reenters the game.  It appeared that the lastChange
	flags in the player visibility structure were nowhere initialised.
    ```

---

### Bug 012

- **Status:** `fixed/low`
- **Reported against version:** `3.0.4`
- **Description:**
  - *931001: Derek C. Richardson (dcr@mail.ast.cam.ac.uk)*

    ```text
    Have you noticed that you can shoot your own tank down and get
	points for it? Nice way to bump up your sccore ((this practice
	is frowned upon here of course).
    ```
- **Work Log:**
  - *931001: bert*

    ```text
    This was true for maps without player shielding (noshields)
	were tanks didn't have shields either.  Changed it to have
	tanks three seconds of shields after they are released.
	This makes it difficult to shoot your own tank.
	The OBJ_SHIELD flag was set in the status field instead
	of the used field.  Ouch!
    ```
  - *931116: bert*

    ```text
    The issue that remains is that a detached tank has very little
	fuel, so it can still be shot down easily if the player is
	persistent enough.  Dunno if we should care about that.
	Being able to cheat is something I tend to like in a nice game :-)
    ```

---

### Bug 013

- **Status:** `fixed/high`
- **Reported against version:** `3.0.4`
- **Description:**
  - *931004: snil@daimi.aau.dk (Sven Nielsen)*

    ```text
    This Zombie BUG can be quite annoying! One evening I was playing
	tournament on a norweigean server when it happened to Data. After I
	had quit he was still in the game. I later came back (had to invent
	the name Ups!) and saw that Zombie Mr. Data was really spoiling the
	game. It didn't take long for the other players to find out that Data
	and Ups!  where from the same host and I got accused of
	cheating/ruining the game on purpose. I tried different things to
	get my Zombie out but that was impossible. The owner of the server
	process was not logged on so no-one had permission to kick him.
	This BUG can be pretty annoying.
	I don't know too much about the UDP protocol, but I imagine, that if
	the packet containing QUIT! is lost then a thing like that may happen.
	How about inventing an acknowledgement scheme or a way to kick your
	*own* pilots ???
    ```
- **Work Log:**
  - *931004: bert*

    ```text
    The client/server protocol had a mechanism to automatically kick
	players out of the game if they didn't respond for about 40 seconds.
	I don't understand why this isn't working anymore (it used to work!).
    ```
  - *911116: bert*

    ```text
    Fixed.  It was one of the most important fixes in 3.0.6.
    ```

---

### Bug 014

- **Status:** `open/low`
- **Reported against version:** `3.0.5`
- **Description:**
  - *931005: Mark Boyns*

    ```text
    I have a problem with robot's Ids disappearing.  Sometimes I will see
	a robot flying around without a name.  The problem seems to be sort
	of random, and I have not spent any time trying to figure out why.
    ```

---

### Bug 015

- **Status:** `fixed/low`
- **Reported against version:** `3.0.5`
- **Description:**
  - *931020: pery@hprnd.rose.hp.com (Pery Pearson)*

    ```text
    I usually leave the xpilot server up for as long as it will stay up
	(it tends to crash after a day or two 8).  I just pause when not
	playing.  Sometimes my fuel will be depleted when I return even if I
	had a full tank when I paused.
    ```
- **Work Log:**
  - *931020: bert*

    ```text
    It could be the case that at some places in the code
	a check for the player being paused is missing.
    ```
  - *931021: Mr M J Cleaton*

    ```text
    The solution is simple - deactivate your shields before pausing.
    ```
  - *931116: bert*

    ```text
    If I remember correctly this has been fixed in 3.0.6.
    ```

---

### Bug 016

- **Status:** `open/low`
- **Reported against version:** `3.0.6`
- **Description:**
  - *931110: kenrsc@stud.cs.uit.no (Ken Ronny Schouten)*

    ```text
    If you play in limited lives mode and commit suicide you will lose
	a life but it will not show on the score list. I.e. if you commit
	suicide once when you have 3 lives the score list will tell you 3
	but you have only 2.
    ```
- **Work Log:**
  - *940204: kenrsc@stud.cs.uit.no (Ken Ronny Schouten)*

    ```text
    Fixed it. In update.c we have the test if the player comitted
        suicide, but we forgot to add the line updateScores = true.
    ```

---

### Bug 019

- **Status:** `fixed/medium`
- **Reported against version:** `3.0.7`
- **Description:**
  - *940113: ferhati@aremihp.univ-lille1.fr ( Ramdane FERHATI )*

    ```text
    In a team mode, if you are in pause mode
	and the other team blow up all your targets
	you automatically quit the pause mode,
	the "P" character is still  before your nick name.
	You must press twice the "P" key to return on
	the pause mode.
    ```
- **Work Log:**
  - *940115: bert*

    ```text
    I think I know where to look for a fix (collision.c, targetKillTeam).
    ```

---

### Bug 020

- **Status:** `fixed`
- **Reported against version:** `3.0.7`
- **Description:**
  - *940211: ferhati@aremihp.univ-lille1.fr ( Ramdane FERHATI )*

    ```text
    Ramdane FERHATI: Story of a bug !    11 Feb 1994 11:35
	I played yesterday, a tournament, my boss arrived in
	my office, so I put my ship in the pause mode and
	iconified the xpilot's window.
	I used another working window to have a hidden icon.
	I spoke with my boss, SUDDENLY, the tournament map
	appeared in the full screen !!!!!!
	AAAAAAAAAAAARRRRRRRRRRRRRRRRRRRRHHHHHHHHHHHHHHHHHHHHHH
	(certainly because the current game was won by a player)
	is-it possible to add a hot-key (Escape key), to avoid
	a heart attack to my boss ?
    ```
  - *940213: kenrsc*

    ```text
    We also have the problem in 3.1 that the popup does not work
        at all. We also need a 'boss' button for this case. Next time 
        he may not have time to land his ship and pause it :)
    ```
- **Work Log:**
  - *940211: bert*

    ```text
    This should not happen.  Paused players should remain paused
	even if the game is restarted.
    ```
  - *940225: kenrsc*

    ```text
    Fixed the popup bug in 3.1. It was due to that the status field
        sent from server to client is a byte big. The GAME_OVER bit
        was set to 11..... This was not the case in version 3.0.7, there
        we had the following line.
	   #define GAME_OVER 8    /* This must not be above 8 */
        So my question is: How was it that moved it to 11 and removed the
        comment ? I should think that a warning like that in the comment
        should prevent it !
    ```
  - *940226: kenrsc*

    ```text
    Fixed Ramdane Ferhati's problem :) We only popup the window if 
        game is over and the player is NOT paused.
    ```

---

### Bug 021

- **Status:** `fixed/medium`
- **Reported against version:** `3.0.7`
- **Description:**
  - *940309: Tony Plate <tap@cs.toronto.edu>*

    ```text
    Suppose we have 3 players, A and B, on one team, and X on the other.
	This is how the bug manifests itself (I think):
	    A is locked onto X.
	    X is locked onto B.
	    A fires a smart at X.
	    X ECMs the smart, now it locks onto B.
	    Smart chases B, but can never hit B, because B is
	    immune to this smart because it was fired by a
	    team member.
	My suggestion: change effect of team immunity so that it
	applies only to simple shots.  (Would be a pretty simple fix).
    ```
- **Work Log:**
  - *941021: Bert*

    ```text
    If I remember correctly now smart missiles can only be redirected
	to the sender.
    ```

---

### Bug 023

- **Status:** `fixed/medium`
- **Reported against version:** `3.1.3`
- **Description:**
  - *940504: kenrsc@stud.cs.uit.no (Ken Ronny Schouten)*

    ```text
    If the server is not in RawMode (Robots playing even if humans is 
        not there) and NoQuit is also set the server will be idle until
        the first player shows up. This also means that the server will
        not send info to the meta server during this time. So after about
        16 minutes the server will no longer be in the meta server list.
        I don't think this is what the starters of the long term servers
        want.
<FIX>   Set an alarm for about 6-10 minutes so the server breaks the idle
        hang to send a message to the meta server. This means some 
        changing in the way signals are handled.
    ```

---

### Bug 024

- **Status:** `fixed/medium`
- **Reported against version:** `3.2.2`
- **Description:**
  - *940523: Frenchy*

    ```text
    When the target blows up and the game resets then the target
	still exists, but it is untouchable.
	Sometimes the target appears in red instead of blue, even
	if it belongs to the player's team.
    ```

---

### Bug 026

- **Status:** `fixed/medium`
- **Reported against version:** `3.2.4`
- **Description:**
  - *940722: Berne Yves-Henri <Yves-Henri.Berne@imag.fr>*

    ```text
    I don't know if you received this bug before but :
	on the map tourmination, I began the game with 7 shots max but suddenly (I
	haven't determine how) I have no limit (as much shots as I want)
	And my friend (who played with the same program) stay with 7 shots max.
    ```

---

### Bug 028

- **Status:** `fixed/medium`
- **Reported against version:** `3.2.6`
- **Description:**
  - *940928: Bretton Wade <bwade@graphics.cornell.edu> writes:*

    ```text
    when I unpause, the shields drop regardless of whether or not the space bar is
	held down. This is a serious disdvantage when the robots are programmed to kill
	a paused player, because they will hover around the player shooting, thus it
	will be impossible to unpause without dying.
    ```
- **Work Log:**
  - *940928: Bert*

    ```text
    You mean when you pause/unpause on your homebase or when
	you pause/unpause somewhere else?  In the former case it seems
	to work well, in the latter you are right that shields are
	dropped and that it should be changed to shields up.
    ```

---

### Bug 30

- **Status:** `closed/low`
- **Reported against version:** `3.2.7`
- **Description:**
  - *941111: border@nas.nasa.gov (Ryan A. Border)*

    ```text
    If you kill a robot, and the server subsequently decides that the
	robot has played to badly to continue, then the robot leaves the
	game.  The problem is that the robot leaves the game *before*
	it explodes... so their is no explosion resulting from the
	kill.  Often times the player making the kill begins thrusting
	to counter-act the effects of the (non-existent) explosion, which
	can sometimes be a lethal mistake.
	Robots should explode when you kill them, even if they're leaving
	the game.
    ```
- **Work Log:**
  - *941211: Bert*

    ```text
    Fixed.
    ```

---

### Bug 33

- **Status:** `closed/low`
- **Reported against version:** `3.2.8`
- **Description:**
  - *941115: Christian Engstler*

    ```text
    .. it's possible to tractor team mates, when
	team immunity is on.
    ```
- **Work Log:**
  - *941201: Harvey Thompson*

    ```text
    You can tractor anyone you can get a lock on, intentionally.  However, getting
	a lock on someone on your on team involves using the left/right arrows.
	It could be useful if your team member has run out of fuel, you could 
	occasionally tractor him back to base.
	I can't see the problem, you specify who you want to tractor, so its not
	as if you accidentally tractor your team mate!
    ```

---

### Bug 35

- **Status:** `closed/low`
- **Reported against version:** `3.2.9`
- **Description:**
  - *941125: Christian Engstler*

    ```text
    ERASE doesn't seem to work with shipshapes.
    ```
- **Work Log:**
  - *941130: Bert*

    ```text
    It seems you're right.
    ```
  - *941213: Bert*

    ```text
    Fixed.
    ```

---

### Bug 36

- **Status:** `closed/medium`
- **Reported against version:** `3.2.9`
- **Description:**
  - *941127: Christian Engstler*

    ```text
    There is a potential bug in 3.2.9 concerning treasures.
	Recently I played teamtourney (bloods) when I replaced 
	my treasure (team 3) and stole 2's. When Pit (team 4) stole
	mine afterwards the game ended with the message
	"team 4 has won the game by destroying 1 treasure and replacing 1"
	or the like. I asked Pit, who confirmed that he didn't replace a
	treasure. What do you think ?
    ```
- **Work Log:**
  - *941130: Bert*

    ```text
    It is not a bug, but the word "saving" was confusing.
	Replaced the word "saving" with "defended".
    ```

---

