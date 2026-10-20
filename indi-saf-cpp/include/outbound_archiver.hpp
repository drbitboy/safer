// outbound_archiver.hpp
//
// Bundles the outbound mailbox's pending files into a single zip
// archive for a separate "link sender" process to transfer.
//
// Directory layout (outboundDir is the FileMailbox directory that
// saf_local's step C upserts into; linkDir is separate so the mailbox
// code never sees the archive files):
//
//   <outboundDir>/ready.<type>.<dev>.<prop>.<elem>     written by FileMailbox::upsert()
//   <outboundDir>/sending.<type>.<dev>.<prop>.<elem>   claimed by the archiver
//   <linkDir>/init.zip      archive being written (archiver only)
//   <linkDir>/sending.zip   finished archive; the link sender process
//                           transfers it, then deletes it once the
//                           far end acknowledges receipt
//
// Only filenames with EXACTLY five '.'-separated fields count as
// mailbox records (state.type.dev.prop.elem); anything else in
// outboundDir (e.g. a stray ready.zip) is left strictly alone.
//
// Steps performed by archiveOutbound(), in order:
//   0. If <linkDir>/sending.zip exists, an archive is already in
//      flight: delete any orphaned sending.<5 fields> files in
//      outboundDir (they are already inside that archive; they can
//      only exist if a previous run crashed between steps 4 and 5)
//      and return Busy WITHOUT touching ready.<...> files, so repeated
//      updates keep collapsing latest-value-wins in the meantime.
//      Only the archiver creates sending.zip and the link sender only
//      deletes it, so this check cannot race with anything: between
//      the check and step 4 the file can only disappear, never appear.
//   1. Delete a stale <linkDir>/init.zip (a crashed previous attempt).
//   2. Claim: rename every ready.<5 fields> in outboundDir to
//      sending.<5 fields>. This protects them from being overwritten:
//      a concurrent upsert() for the same key then creates a NEW
//      ready.<...> file next to it, which is archived next cycle. If a
//      leftover sending.<...> for the same key exists from a crash,
//      this rename replaces it with the newer value (latest-wins).
//   3. Write <linkDir>/init.zip containing every sending.<5 fields>
//      file, each stored under the entry name init.<type>.<dev>.<prop>.<elem>
//      (so the receiving side can extract it and rename each entry to
//      ready.<...> exactly as FileMailbox::upsert() would). fsync the
//      zip and the directory.
//   4. rename() init.zip -> sending.zip (atomic; the link sender can
//      never observe a partial archive), fsync the directory.
//   5. Delete the sending.<5 fields> source files.
//
// A crash anywhere before step 4 loses nothing: the sources are still
// present and the next call redoes steps 1-4. A crash between 4 and 5
// is handled by step 0 above (or, if the link sender already finished
// and deleted sending.zip, the orphans are simply re-archived; that
// resends values the far end already has, which is harmless).
//
// Single-caller assumption: at most one archiver (thread or process)
// operates on a given outboundDir/linkDir pair.

#pragma once
#include <string>

namespace saf {

enum class ArchiveResult {
    Busy,        // sending.zip already exists; nothing done
    NothingToDo, // no ready/sending mailbox files to archive
    Archived,    // sending.zip created
    Error,       // see ArchiveReport::error; sources left intact
};

struct ArchiveReport {
    ArchiveResult result = ArchiveResult::NothingToDo;
    int fileCount = 0;  // records archived (Archived) or orphans removed (Busy)
    std::string error;  // set when result == Error
};

// True iff name is exactly "<state>.<f1>.<f2>.<f3>.<f4>" with the given
// state prefix, i.e. five '.'-separated fields, first == state.
bool isMailboxFilename(const std::string& name, const std::string& state);

ArchiveReport archiveOutbound(const std::string& outboundDir,
                              const std::string& linkDir);

} // namespace saf
