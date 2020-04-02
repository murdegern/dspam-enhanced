// $Id: dspam.js,v 1.02 2009/12/25 03:00:40 sbajic Exp $

// Select intermediate checkboxes on shift-click
var shifty = false;
var lastcheckboxchecked = false;
function checkboxclicked(checkbox)
{
	var num = checkbox.id.split('-')[1];

	if (lastcheckboxchecked && shifty) {
		var start = Math.min(num, lastcheckboxchecked) + 1;
		var end   = Math.max(num, lastcheckboxchecked) - 1;
		var value = checkbox.checked;
		for (i=start; i <= end; ++i) {
			document.getElementById("checkbox-"+i).checked = value;
			changebgcolor(i, value);
		}
	}
	lastcheckboxchecked = num;
	changebgcolor(num, checkbox.checked);
}

function recordshiftiness(e)
{
	if (!e) var e = window.event;
	shifty = e && ((typeof (e.shiftKey) != 'undefined' && e.shiftKey) ||
		       e.modifiers & event.SHIFT_MASK);
}

function changebgcolor(i, checked)
{
	tr = document.getElementById("rid"+i);
	if (checked)
	{
		tr.className = "selected";
	}
	else
	{
		tr.className = ((i%2)!=0) ? "rowEven" : "rowOdd";
	}
}

function clickcheckbox(i)
{
	var checkbox = document.getElementById("checkbox-"+i);
	checkbox.checked = !checkbox.checked;
	checkboxclicked(checkbox);
}

if (window.event) document.captureEvents (event.MOUSEDOWN);
document.onmousedown = recordshiftiness;
