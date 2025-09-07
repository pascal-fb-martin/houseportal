// Copyrigth Pascal Martin, 2025
//
// Capture web API client.
//
// This client code matches the web API in houseportal/housecapture.c
//

var captureURLbase = null;
var captureRunning = false;
var captureLatestKnown = 0;

const dateOption = {year: 'numeric',
                    month: 'numeric',
                    day: 'numeric',
                    hour: 'numeric',
                    minute: 'numeric',
                    second: 'numeric',
                    hour12: false,
                    fractionalSecondDigits:3};

function captureNewColumn (text) {
   var column = document.createElement("td");
   column.innerHTML = text;
   return column;
}

function captureRow (table, event) {
   var timestamp = new Date(event[0]);
   var row = table.insertRow();
   row.appendChild(captureNewColumn(timestamp.toLocaleString('en-US', dateOption)));
   row.appendChild(captureNewColumn(event[1]));
   row.appendChild(captureNewColumn(event[2]));
   row.appendChild(captureNewColumn(event[3]));
   return row;
}

function captureChangeState (state) {

   captureRunning = state;

   var elmt = document.getElementById ('capture-onoff');
   if (state)
      elmt.innerHTML = "Stop Capture"; // next click.
   else
      elmt.innerHTML = "Start Capture"; // next click.

   elmt = document.getElementById ('capture-category');
   elmt.disabled = state;
   elmt = document.getElementById ('capture-act');
   elmt.disabled = state;
   elmt = document.getElementById ('capture-data');
   elmt.disabled = state;
}

function captureShow (response) {

   var table = document.getElementById ('capturelist');
   for (var i = table.rows.length - 1; i > 1; i--) {
      table.deleteRow(i);
   }
   if (response.invert) {
      var end = response.capture.length;
      for (var i = 0; i < end; ++i) {
         captureRow(table, response.capture[i]);
      }
   } else {
      for (var i = response.capture.length-1; i >= 0; --i) {
         captureRow(table, response.capture[i]);
      }
   }
   if (response.latest) captureLatestKnown = response.latest;
}

function captureUpdate () {

   if (!captureRunning) return;

   var url = captureURLbase + "/capture/get";
   if (captureLatestKnown) url += "?known=" + captureLatestKnown;

   var command = new XMLHttpRequest();
   command.open("GET", url);
   command.onreadystatechange = function () {
       if (command.readyState === 4) {
           if (command.status === 200) {
               captureShow (JSON.parse(command.responseText));
           } else if (command.status === 409) {
               captureChangeState (false); // Stop.
           }
       }
   }
   command.send(null);
}

function captureFilter () {

   var prefix = '?';
   var param = '';
   var input = document.getElementById ('capture-category');
   var selected = input.value;
   if (selected && (selected !== 'all')) {
      param += prefix + 'cat=' + selected;
      prefix = '&';
   }
   input = document.getElementById ('capture-act');
   if (input.value) {
      param += prefix + 'act=' + input.value;
      prefix = '&';
   }
   input = document.getElementById ('capture-data');
   if (input.value) {
      param += prefix + 'data=' + input.value;
      // prefix = '&'; // not needed as long as this is the last parameter.
   }
   return param;
}

function captureOnOff () {

   var uri;
   var disable;
   if (captureRunning) {
      uri = '/stop';
      disable = false;
   } else {
      uri = '/start' + captureFilter();
      disable = true;
   }

   captureChangeState (disable);

   var command = new XMLHttpRequest();
   command.open("GET", captureURLbase + "/capture" + uri);
   command.send(null);
}

function capturePrepare (response) {

   var title = response.host + ' - ' + captureURLbase.substring(1) + ' Capture';
   document.getElementsByTagName ('title')[0].innerHTML = title;

   var table = document.getElementById ('capturelist');
   for (var i = table.rows.length - 1; i > 0; i--) {
      table.deleteRow(i);
   }
   // The first row is actually the capture filter.
   var row = table.insertRow();

   // No timestamp filtering.
   var column = document.createElement("td");
   row.appendChild (column);

   column = document.createElement("td");
   var input = document.createElement ('select');
   input.id = 'capture-category';
   var opt = document.createElement ('option');
   opt.value = 'all';
   opt.innerHTML = 'All';
   input.appendChild(opt);
   var categories = response.capture.sort();
   for (var i = 0; i < categories.length; ++i) {
      opt = document.createElement ('option');
      opt.label = categories[i];
      opt.innerHTML = categories[i];
      input.appendChild(opt);
   }
   column.appendChild(input);
   row.appendChild (column);

   column = document.createElement("td");
   input = document.createElement('input');
   input.type = 'text';
   input.id = 'capture-act';
   input.name = 'action';
   input.placeholder = 'Substring';
   column.appendChild(input);
   row.appendChild (column);

   column = document.createElement("td");
   input = document.createElement('input');
   input.type = 'text';
   input.id = 'capture-data';
   input.name = 'data';
   input.style = 'width: 60%;';
   input.placeholder = 'Substring';
   column.appendChild(input);
   var span = document.createElement("span");
   span.style = "text-align: left; width: 100%;";
   input = document.createElement('button');
   input.type = 'button';
   input.id = 'capture-onoff';
   input.style = 'text-align: right;';
   input.innerHTML = 'Start Capture';
   input.onclick = captureOnOff;
   span.appendChild(input);
   column.appendChild(span);
   row.appendChild (column);
}

function captureStart (url) {
   if (url)
       captureURLbase = url;
   else
       captureURLbase = '/portal';

   setInterval (captureUpdate, 1000);

   var command = new XMLHttpRequest();
   command.open("GET", captureURLbase + "/capture/info");
   command.onreadystatechange = function () {
       if (command.readyState === 4 && command.status === 200) {
           capturePrepare (JSON.parse(command.responseText));
       }
   }
   command.send(null);
}

