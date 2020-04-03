// $Id: dspam.js,v 1.02 2009/12/25 03:00:40 sbajic Exp $

// Select intermediate checkboxes on shift-click
var shifty = false;
var lastcheckboxchecked = false;

function checkboxClicked(checkbox) {
  var num = checkbox.id.split('-')[1];

  if (lastcheckboxchecked && shifty) {
    var start = Math.min(num, lastcheckboxchecked) + 1;
    var end = Math.max(num, lastcheckboxchecked) - 1;
    var value = checkbox.checked;
    for (i = start; i <= end; ++i) {
      document.getElementById("checkbox-" + i).checked = value;
      changeBgColor(i, value);
    }
  }
  lastcheckboxchecked = num;
  changeBgColor(num, checkbox.checked);
}

function recordShiftiness(e) {
  if (!e) var e = window.event;
  shifty = e && ((typeof(e.shiftKey) != 'undefined' && e.shiftKey) ||
    e.modifiers & event.SHIFT_MASK);
}

function changeBgColor(i, checked) {
  tr = document.getElementById("rid" + i);
  if (checked) {
    tr.className = "selected";
  } else {
    tr.className = ((i % 2) != 0) ? "rowEven" : "rowOdd";
  }
}

function clickCheckbox(i) {
  var checkbox = document.getElementById("checkbox-" + i);
  checkbox.checked = !checkbox.checked;
  checkboxClicked(checkbox);
}

function unselectAllCheckboxes() {
  var cbs = document.getElementsByTagName("input");
  for (i = 0; i < cbs.length; ++i) {
    if (cbs[i].type == "checkbox" && cbs[i].id.startsWith("checkbox-")) {
      cbs[i].checked = false;
      changeBgColor(cbs[i].id.split("-")[1], false);
    }
  }
}

function regMark() {
  var regex = document.getElementById("reg_mark_pattern").value;
  unselectAllCheckboxes();
  if (regex !== "") {
    var froms = document.getElementsByClassName("reg_mark_from");
    for (i = 0; i < froms.length; ++i) {
      from = froms[i].innerHTML.replace(/^.+&lt;/, "").replace(/&gt;.*$/, "");
      matches = from.match(regex);
      if (matches != null && (matches.length > 1 || matches[0] != "")) {
        var cb_id = froms[i].id.split('-')[1];
        clickCheckbox(cb_id)
      }
    }
  }
}

function regMarkPressed(e) {
  var enterPressed = ((e.code !== undefined && e.code == "Enter") || (e.which !== undefined && e.which == 13) || e.keyCode == 13);
  if (enterPressed) {
    e.preventDefault();
    regMark();
  }
}

if (window.event)
  document.captureEvents(event.MOUSEDOWN);
document.onmousedown = recordShiftiness;