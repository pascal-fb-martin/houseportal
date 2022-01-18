// Copyrigth Pascal Martin, 2022
//
// Event web API client.
//
// This client code matches the web API implemented in houseportal/houselog.c
//

var eventURLbase = null;
var eventLastId = new Array();

function eventShow (response) {

   function eventNewColumn (text) {
      var column = document.createElement("td");
      column.innerHTML = text;
      return column;
   }

   var app = response.apps[0]; // For now handle only one app per query.

   if (!eventLastId[app]) {
      var name = app[0].toUpperCase() + app.substring(1);
      var title = response.host + ' - ' + name + ' Events';
      document.getElementsByTagName ('title')[0].innerHTML = title;
      var elements = document.getElementsByClassName ('hostname');
      for (var i = 0; i < elements.length; i++) {
          elements[i].innerHTML = response.host;
      }
      if (response.proxy) {
          document.getElementById('portal').href =
              'http://'+response.proxy+'/index.html';
      } else {
          document.getElementById('portal').href =
              'http://'+response.host+'/index.html';
      }
   }

   eventLastId[app] = response[app].latest;

   var table = document.getElementsByClassName ('eventlist')[0];
   for (var i = table.childNodes.length - 1; i > 1; i--) {
      table.removeChild(table.childNodes[i]);
   }
   for (var i = response[app].events.length-1; i >= 0; --i) {
      var event = response[app].events[i];
      var timestamp = new Date(event[0]);
      var row = document.createElement("tr");
      row.appendChild(eventNewColumn(timestamp.toLocaleString()));
      row.appendChild(eventNewColumn(event[1]));
      row.appendChild(eventNewColumn(event[2]));
      row.appendChild(eventNewColumn(event[3]));
      row.appendChild(eventNewColumn(event[4]));
      table.appendChild(row);
   }
}

function eventUpdate() {

   var command = new XMLHttpRequest();
   command.open("GET", eventURLbase + "/log/events");
   command.onreadystatechange = function () {
      if (command.readyState === 4 && command.status === 200) {
         eventShow (JSON.parse(command.responseText));
      }
   }
   command.send(null);
}

function eventCheck () {

   var response = null;

   function eventNeedUpdate (result, value) {
      if (result) return true;
      if (! eventLastId[value]) return true;
      if (response[value].latest != eventLastId[value]) return true;
      return false;
   }

   var command = new XMLHttpRequest();
   command.open("GET", eventURLbase + "/log/latest");
   command.onreadystatechange = function () {
       if (command.readyState === 4 && command.status === 200) {
           response = JSON.parse(command.responseText);
           if (response.apps.reduce(eventNeedUpdate, false)) eventUpdate ();
       }
   }
   command.send(null);
}

function eventStart (url) {
   if (url)
       eventURLbase = url;
   else
       eventURLbase = '/portal';

   eventUpdate();
   setInterval (eventCheck, 1000);
}

