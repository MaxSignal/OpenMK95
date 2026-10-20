// Interfaces between the game modules (map, events, menus, battle, audio, save).
#pragma once
#include "engine/state.h"
#include "engine/ui.h"

namespace mapx {
void setupPartyCharacters();
void teleport(int map, int x, int y);
void recomputeScrollFlags();
void clearGrid();
// Tick render (04 §10.3) with direction D and scroll sub-step S.
void tickRender(int D, int S);
// Interim redraw (04 §10.4).
void interimRedraw(bool present);
void fullTileRedraw();
void startMotion(Chara* c, int m);
Chara* advance(Chara* c);
// per-tick update of a vehicle or map event (04 §8.1); returns false if the
// remaining map events must be skipped (pass-abort set by a resumed script)
void updateObject(Chara* e, bool isMapEvent);
// one route step: true = route ended
bool routeNext(Chara* c);
void applyLeadingDisplayBytes(Chara* c);
void heroControlStep();   // 01 §8 step 7
void actionButton();
bool alight();            // try alighting; true on success
void bumpTouch(Chara* e);
void stepOnCheck();
void faceEachOther(Chara* e, bool layerTest);
void poisonStep();
void encounterStep();
void finishBoarding();
void finishAlighting();
void boardVehicle(Chara* obj);
bool escapeProcedure(int area);
int vehicleLoopStart();
bool vehicleUpdatable(int k);
void deleteMapEvents();
void resetTrail();
void gatherParty();
bool charaOnRowVisited(const Chara* c);
void heroSubstepTick();  // 01 §8 step 11.1; returns via flags
void mouseDragMove(int x, int y);
void endAutoWalk();
}  // namespace mapx

namespace ev {
void runEvent(int id, Chara* obj);
void refreshAllPages();
void applyPage(Chara* c, bool partial);
void createMapEvents();
void resumeHeroWait();      // hero route ended / alighting finalisation
void expandText(const sjis& in, sjis& out, bool& ok);  // $ codes; sets messageTime
extern int messageTime;     // -1 = none
}  // namespace ev

namespace menu {
extern int subMode;
extern int menuActor;
extern int actionMember;
void runFieldMenu();
void onSelected(int slot, int index, bool byMouse);
void onCancelled(int slot);
void closeMenu();
void shop(const sjis& itemList, const sjis& texts, int buyOnly);
void inn(const sjis& texts, int price, int flags);
void pageChanged(int slot, int page);
void useItem(int item, int user, int target);
void castSpell(int spell, int caster, int target, bool fromItem);
}  // namespace menu

namespace battle {
extern bool autoBattle;
void start(int troop, bool escapeForbidden);
void onSelected(int slot, int index, bool byMouse);
void onCancelled(int slot);
void pageChanged(int slot, int page);
// shared effect code used by the field menu
void useItem(int item, int user, int target);
void castSpell(int spell, int caster, int target, bool fromItem);
void statusCure(Battler& X);
bool teleportTo(int area);
int maxLuck();
}  // namespace battle

namespace audio {
void init();
void playBGM(const sjis& name, const int32_t* ab, bool force);
void stopAllMusic();
void saveBGM();
void resumeBGM(bool force);
void playSE(const sjis& name, const int32_t* ab);
void playBuiltinSE(int n);
void playSlotBGM(int slot);
void playFieldMusic();
void playJingle(int slot);
void waitJingle(int slot);
void setPlayOnce(bool on);
void battleEndUnload();
void recreateDevice();
void shutdownDevice();
void poll();
extern BgmRec cur;
extern BgmRec saved;
}  // namespace audio

namespace save {
bool saveDialog();          // returns after the dialog (saved or cancelled)
bool loadDialog();          // true = loaded
bool writeSave(const sjis& name);
int readSave(const sjis& name);  // 0 ok, 1 cannot open, 2 bad header
int readSaveData(const std::vector<uint8_t>& file);  // readSave from a file image (0 ok, 2 bad)
void buildSave(std::vector<uint8_t>& out);        // the file image writeSave writes
void afterLoadFromMenu();
}  // namespace save

namespace mov {
void playMovie(const sjis& name, int flags);
}
