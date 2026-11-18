#include "UsbStoragePolicy.h"
#include <cassert>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

using namespace ofe_usb;
#include "msc_types.h"
#include "manager_state.h"
State fixture;
State* state = &fixture;
bool mutex = true;
std::atomic<bool> present{true}, ready{true};
unsigned guardDepth = 0, clockValue = 0, handles = 0, maxHandles = 0, renameCalls = 0, unlinkCalls = 0;
bool failRename = false;
struct Guard { Guard() { ++guardDepth; } ~Guard() { --guardDepth; } };
struct String : std::string { using std::string::string; void toCharArray(char* out, size_t n) const { snprintf(out, n, "%s", c_str()); } };
uint32_t manage(const String&, uint32_t, unsigned, unsigned, const char*);
size_t strlcpy(char* out, const char* in, size_t n) { if (n) snprintf(out, n, "%s", in); return strlen(in); }
uint32_t millis() { return clockValue; }
void vTaskDelay(unsigned ticks) { assert(!guardDepth); clockValue += ticks; }
void message(const char* text, Phase phase) { Guard guard; strlcpy(state->message, text, sizeof(state->message)); state->phase = phase; }
bool busy() { return state->phase == Phase::Queued || state->phase == Phase::Reading || state->phase == Phase::Writing || state->phase == Phase::Receiving || state->buffer; }

struct Node { bool directory; uint64_t size; std::string identity; };
std::map<std::string, Node> disk;
struct TestStat { int st_mode = 0; int64_t st_size = 0; };
void checkPath(const std::string& path) { assert(!guardDepth); assert(path == "/usb" || path.rfind("/usb/", 0) == 0); assert(path.find("/../") == std::string::npos); }
int testStat(const char* path, TestStat* info) {
  checkPath(path); auto found = disk.find(path);
  if (found == disk.end()) { errno = ENOENT; return -1; }
  info->st_mode = found->second.directory ? 2 : 1; info->st_size = found->second.size; return 0;
}
struct dirent { char d_name[512]{}; };
struct DIR { std::vector<std::string> names; size_t next = 0; dirent current; };
DIR* opendir(const char* path) {
  checkPath(path); assert(state->phase == Phase::Reading || state->phase == Phase::Writing); assert(!manage("mkdir",state->generation,0,0,"during-scan")); auto found = disk.find(path); if (found == disk.end() || !found->second.directory) return nullptr;
  auto* dir = new DIR; dir->names = {".", ".."}; const std::string prefix = std::string(path) + '/';
  for (const auto& pair : disk) if (pair.first.rfind(prefix, 0) == 0 && pair.first.find('/', prefix.size()) == std::string::npos) dir->names.push_back(pair.first.substr(prefix.size()));
  maxHandles = std::max(maxHandles, ++handles); return dir;
}
dirent* readdir(DIR* dir) { assert(!guardDepth); if (dir->next == dir->names.size()) return nullptr; strlcpy(dir->current.d_name, dir->names[dir->next++].c_str(), sizeof(dir->current.d_name)); return &dir->current; }
int closedir(DIR* dir) { assert(!guardDepth); --handles; delete dir; return 0; }
int testMkdir(const char* path, unsigned) { checkPath(path); if (disk.count(path)) { errno = EEXIST; return -1; } disk[path] = {true, 0, "directory"}; return 0; }
int testRename(const char* from, const char* to) {
  checkPath(from); checkPath(to); ++renameCalls;
  if (failRename) { errno = EIO; return -1; }
  assert(!disk.count(to)); const std::string source=from,target=to;std::map<std::string,Node> moved;
  for(auto it=disk.begin();it!=disk.end();) {
    if(it->first==source||it->first.rfind(source+'/',0)==0){moved[target+it->first.substr(source.size())]=it->second;it=disk.erase(it);}else ++it;
  }
  disk.insert(moved.begin(),moved.end());return 0;
}
int testUnlink(const char* path) { checkPath(path); ++unlinkCalls; assert(disk.count(path) && !disk.at(path).directory); disk.erase(path); return 0; }
int testRmdir(const char* path) {
  checkPath(path); assert(std::string(path) != "/usb");
  if (!disk.count(path)) { errno = ENOENT; return -1; }
  const std::string prefix = std::string(path) + '/';
  for (const auto& pair : disk) if (pair.first.rfind(prefix, 0) == 0) { errno = ENOTEMPTY; return -1; }
  disk.erase(path); return 0;
}
#define stat TestStat
#define S_ISDIR(mode) ((mode) == 2)
#define S_ISREG(mode) ((mode) == 1)
#define mkdir testMkdir
#define rename testRename
#define unlink testUnlink
#define rmdir testRmdir
int TestStat(const char* p, struct TestStat* s) { return testStat(p, s); }
#include "manager_fs.h"
#include "manager_queue.h"
#undef stat
#undef mkdir
#undef rename
#undef unlink
#undef rmdir

unsigned directory(const std::string& path) { for (unsigned i = 0; i < state->directoryCount; ++i) if (path == state->directories[i].path) return i; assert(false); return 0; }
unsigned file(const std::string& path) {
  for (unsigned i = 0; i < state->count; ++i) { char full[MAX_PATH]; assert(joinPath(full, sizeof(full), state->directories[state->files[i].directory].path, state->files[i].name)); if (path == full) return i; }
  assert(false); return 0;
}
void operation(const char* action, unsigned index, unsigned dest, const char* name, const char* result) {
  const auto previous = state->completedOperation;const std::string previousResult=state->result;
  const auto id = manage(String(action), state->generation, index, dest, name); assert(id);
  assert(state->completedOperation==previous && previousResult==state->result);
  assert(!manage("mkdir", state->generation, 0, 0, "concurrent"));
  state->phase = Phase::Writing;
  manageSelected(action); assert(state->completedOperation == id && !strcmp(state->result, result));
  assert(state->phase == (!strcmp(result, "OK") ? Phase::Idle : Phase::Error)); assert(!guardDepth && !handles);
}
void reset() { fixture = State{}; disk.clear(); disk["/usb"] = {true,0,"root"}; clockValue=0; present=true; ready=true; }
int main() {
  reset(); disk["/usb/OFE"]={true,0,"dir"}; disk["/usb/OFE/Nested"]={true,0,"dir"}; disk["/usb/Empty"]={true,0,"dir"}; disk["/usb/NotEmpty"]={true,0,"dir"};
  disk["/usb/NotEmpty/.hidden"]={false,5,"hidden"}; disk["/usb/System Volume Information"]={true,0,"system"};
  disk["/usb/source.txt"]={false,4,"original"}; disk["/usb/OFE/Nested/existing.txt"]={false,7,"preserved"};
  disk["/usb/OFE/Nested/zero.txt"]={false,0,"empty"}; disk["/usb/OFE/Nested/photo (1).jpg"]={false,100,"picture"};
  refresh(); assert(state->count == 4 && state->directoryCount == 5 && !state->truncated); assert(maxHandles == 1);
  assert(!manage("rmdir", state->generation, 0, 0, ""));
  assert(!manage("delete", state->generation-1, 0, 0, ""));
  assert(!manage("mkdir", state->generation, 0, 0, "../bad"));
  operation("mkdir",0,directory("OFE/Nested"),"New folder","OK"); assert(disk.count("/usb/OFE/Nested/New folder"));
  operation("mkdir",0,directory("OFE/Nested"),"New folder","EXISTS");
  auto renames=renameCalls;
  operation("move",file("source.txt"),directory("OFE/Nested"),"existing.txt","EXISTS");
  assert(renameCalls==renames && disk.at("/usb/source.txt").identity=="original" && disk.at("/usb/OFE/Nested/existing.txt").identity=="preserved");
  operation("move",file("source.txt"),directory("OFE/Nested"),"moved (1).txt","OK");
  assert(!disk.count("/usb/source.txt") && disk.at("/usb/OFE/Nested/moved (1).txt").identity=="original");
  operation("move",file("OFE/Nested/moved (1).txt"),directory("OFE/Nested"),"moved (1).txt","SAME_PATH");
  failRename=true;operation("move",file("OFE/Nested/moved (1).txt"),0,"failed.txt","IO"); failRename=false;
  assert(disk.count("/usb/OFE/Nested/moved (1).txt") && !disk.count("/usb/failed.txt"));
  operation("rmdir",directory("NotEmpty"),0,"","NOT_EMPTY"); assert(disk.count("/usb/NotEmpty/.hidden"));
  operation("rmdir",directory("Empty"),0,"","OK");assert(!disk.count("/usb/Empty"));
  disk["/usb/OFE/Nested/moved (1).txt"].size=123;auto unlinks=unlinkCalls;
  operation("delete",file("OFE/Nested/moved (1).txt"),0,"","STALE");assert(unlinkCalls==unlinks);
  refresh();operation("delete",file("OFE/Nested/moved (1).txt"),0,"","OK");assert(!disk.count("/usb/OFE/Nested/moved (1).txt"));
  assert(!manage("move_dir",state->generation,0,0,"root"));
  operation("move_dir",directory("OFE"),directory("OFE/Nested"),"Bad","SUBTREE");
  operation("move_dir",directory("OFE"),0,"NotEmpty","EXISTS");
  operation("move_dir",directory("OFE"),0,"OFE","SAME_PATH");
  operation("move_dir",directory("OFE"),0,"Renamed","OK");
  assert(disk.at("/usb/Renamed/Nested/existing.txt").identity=="preserved");
  operation("move_dir",directory("Renamed/Nested"),0,"Moved","OK");
  assert(disk.count("/usb/Moved/zero.txt")&&!disk.count("/usb/Renamed/Nested"));
  operation("move_dir",directory("Moved"),directory("Renamed"),"Nested","OK");
  operation("move_dir",directory("Renamed"),0,"OFE","OK");
  state->truncated=true;operation("move_dir",directory("OFE"),0,"No","LIMIT");state->truncated=false;
  auto id=manage("delete",state->generation,file("OFE/Nested/zero.txt"),0,"");assert(id);present=false;manageSelected("delete");
  assert(!strcmp(state->result,"DISCONNECTED") && disk.count("/usb/OFE/Nested/zero.txt"));
  reset();std::string parent;for(unsigned i=0;i<5;++i){parent+=(parent.empty()?"":"/")+std::string("level");disk["/usb/"+parent]={true,0,"dir"};}
  disk["/usb/Source"]={true,0,"dir"};disk["/usb/Source/Child"]={true,0,"dir"};disk["/usb/Source/Child/data.txt"]={false,4,"preserved"};refresh();
  operation("move_dir",directory("Source"),directory(parent),"Deep","PATH");assert(disk.count("/usb/Source/Child/data.txt"));
  disk.erase("/usb/Source");operation("move_dir",directory("Source"),0,"Missing","STALE");
  reset();for(unsigned i=0;i<MAX_FILES+4;++i)disk["/usb/file"+std::to_string(i)+".dat"]={false,4,"data"};refresh();assert(state->count==MAX_FILES && state->truncated);
  reset();for(unsigned i=0;i<MAX_DIRECTORIES+4;++i)disk["/usb/dir"+std::to_string(i)]={true,0,"dir"};refresh();assert(state->directoryCount==MAX_DIRECTORIES && state->truncated);
  reset();std::string deep;for(unsigned i=0;i<MAX_DEPTH+2;++i){deep+=(deep.empty()?"":"/")+std::string("level");disk["/usb/"+deep]={true,0,"dir"};}refresh();assert(state->directoryCount==MAX_DEPTH+1 && state->truncated);
  assert(!handles && !guardDepth);puts("PASS: actual USB breadth-first scanner and worker actions; nested/empty folders, name collisions, stale selections, disconnects, IO errors, no overwrite/recursive deletion, bounded PSRAM inventory and one open directory");
}
