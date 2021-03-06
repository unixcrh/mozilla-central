Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://services-sync/engines.js");
Cu.import("resource://services-sync/constants.js");
Cu.import("resource://services-sync/engines/history.js");
Cu.import("resource://services-sync/util.js");

function onScoreUpdated(callback) {
  Svc.Obs.add("weave:engine:score:updated", function observer() {
    Svc.Obs.remove("weave:engine:score:updated", observer);
    try {
      callback();
    } catch (ex) {
      do_throw(ex);
    }
  });
}

Engines.register(HistoryEngine);
let engine = Engines.get("history");
let tracker = engine._tracker;

let _counter = 0;
function addVisit() {
  let uri = Utils.makeURI("http://getfirefox.com/" + _counter);
  PlacesUtils.history.addVisit(uri, Date.now() * 1000, null, 1, false, 0);
  _counter++;
  return uri;
}


function run_test() {
  run_next_test();
}

add_test(function test_empty() {
  _("Verify we've got an empty tracker to work with.");
  do_check_empty(tracker.changedIDs);
  do_check_eq(tracker.score, 0);
  run_next_test();
});

add_test(function test_not_tracking(next) {
  _("Create history item. Won't show because we haven't started tracking yet");
  addVisit();
  Utils.nextTick(function() {
    do_check_empty(tracker.changedIDs);
    do_check_eq(tracker.score, 0);
    run_next_test();
  });
});

add_test(function test_start_tracking() {
  _("Tell the tracker to start tracking changes.");
  onScoreUpdated(function() {
    do_check_attribute_count(tracker.changedIDs, 1);
    do_check_eq(tracker.score, SCORE_INCREMENT_SMALL);
    run_next_test();
  });
  Svc.Obs.notify("weave:engine:start-tracking");
  addVisit();
});

add_test(function test_start_tracking_twice() {
  _("Notifying twice won't do any harm.");
  onScoreUpdated(function() {
    do_check_attribute_count(tracker.changedIDs, 2);
    do_check_eq(tracker.score, 2 * SCORE_INCREMENT_SMALL);
    run_next_test();
  });
  Svc.Obs.notify("weave:engine:start-tracking");
  addVisit();
});

add_test(function test_track_delete() {
  _("Deletions are tracked.");
  let uri = Utils.makeURI("http://getfirefox.com/0");
  let guid = engine._store.GUIDForUri(uri);
  do_check_false(guid in tracker.changedIDs);

  onScoreUpdated(function() {
    do_check_true(guid in tracker.changedIDs);
    do_check_attribute_count(tracker.changedIDs, 3);
    do_check_eq(tracker.score, SCORE_INCREMENT_XLARGE + 2 * SCORE_INCREMENT_SMALL);
    run_next_test();
  });
  do_check_eq(tracker.score, 2 * SCORE_INCREMENT_SMALL);
  PlacesUtils.history.removePage(uri);
});

add_test(function test_dont_track_expiration() {
  _("Expirations are not tracked.");
  let uriToExpire = addVisit();
  let guidToExpire = engine._store.GUIDForUri(uriToExpire);
  let uriToRemove = addVisit();
  let guidToRemove = engine._store.GUIDForUri(uriToRemove);

  tracker.clearChangedIDs();
  do_check_false(guidToExpire in tracker.changedIDs);
  do_check_false(guidToRemove in tracker.changedIDs);

  onScoreUpdated(function() {
    do_check_false(guidToExpire in tracker.changedIDs);
    do_check_true(guidToRemove in tracker.changedIDs);
    do_check_attribute_count(tracker.changedIDs, 1);
    run_next_test();
  });

  // Observe expiration.
  Services.obs.addObserver(function onExpiration(aSubject, aTopic, aData) {
    Services.obs.removeObserver(onExpiration, aTopic);
    // Remove the remaining page to update its score.
    PlacesUtils.history.removePage(uriToRemove);
  }, PlacesUtils.TOPIC_EXPIRATION_FINISHED, false);

  // Force expiration of 1 entry.
  Services.prefs.setIntPref("places.history.expiration.max_pages", 0);
  Cc["@mozilla.org/places/expiration;1"]
    .getService(Ci.nsIObserver)
    .observe(null, "places-debug-start-expiration", 1);
});

add_test(function test_stop_tracking() {
  _("Let's stop tracking again.");
  tracker.clearChangedIDs();
  Svc.Obs.notify("weave:engine:stop-tracking");
  addVisit();
  Utils.nextTick(function() {
    do_check_empty(tracker.changedIDs);
    run_next_test();
  });
});

add_test(function test_stop_tracking_twice() {
  _("Notifying twice won't do any harm.");
  Svc.Obs.notify("weave:engine:stop-tracking");
  addVisit();
  Utils.nextTick(function() {
    do_check_empty(tracker.changedIDs);
    run_next_test();
  });
});

add_test(function cleanup() {
   _("Clean up.");
  PlacesUtils.history.removeAllPages();
  tracker._lazySave.clear();
  run_next_test();
});
