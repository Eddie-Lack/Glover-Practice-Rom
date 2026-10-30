#ifndef PRACTICE_SPAWN_H
#define PRACTICE_SPAWN_H

#include "game.h"
#include "types.h"

/* Spawn or move actors: two tools that change the level's actors.
 *
 * Move: on the details page of the actor list (Inspect, Actors), Z brings the chosen actor to Glover, in front
 * of the glove and a little above it (Run.moveActor). A platform that has no motion of its own is made a moving
 * platform for the game (it drops flag 0x20000, as the game does when a platform starts to move). Its
 * collision faces then follow the position. A platform with a path, a spin or a parent goes back to its own motion.
 *
 * Spawn: the Inspect menu's "Spawn actor" view makes a new actor at Glover (Run.spawnKind). The list holds only
 * what the game can make at run time with what the level has loaded:
 * - a garib, or a garib that gives a life: the game makes these when an enemy drops one (KillEnemy);
 * - a power-up, if its model is in the level's object banks: CreateNewPickup, as the level file's pick-ups;
 * - an enemy of a type the level already has: LoadEnemy makes the actor, and the instruction lists (what the
 *   enemy does) are copied from an enemy of that type.
 * Left out: platforms (they need their faces, motion and tags from the level file), landscape and water (their
 * faces are built when the level loads), the bosses and the scripted enemies (their fights run from
 * enemies[0] to enemies[2] and from the level's script), and every enemy type the level has not loaded (the
 * game stops the level when a model is missing). docs/game.md has the reasons.
 *
 * The HUD shows "MODIFIED actors" after each use, until the level changes or restarts. A spawned garib counts as
 * one more garib of the level (numGaribs goes up by one, as it does for a garib in the level file). The game
 * saves the score and the garib count at the end of a level. A collected spawned garib or power-up can put
 * extra points in the save. */

/* The kinds, as Run.spawnKind: 1 + KIND_x. */
#define SPAWN_GARIB 0
#define SPAWN_LIFE 1
#define SPAWN_PICKUP 2   /* + the pick-up number, 0 to 14 */
#define SPAWN_ENEMY 17   /* + the actor type of the enemy */

/* The actor list's name for an actor type. */
const char *ActorTypeName(int type);
/* 1 if the move tool can move the actor. */
int ActorMovable(const Actor *a);
/* Notes that an actor was made, moved or edited in this level: the HUD's MODIFIED marker, until the level ends. */
void ActorsModified(void);
/* The game thread, at the start of each game frame (ActionsGameFrame). */
void SpawnGameFrame(void);

#endif
