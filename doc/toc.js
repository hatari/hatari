/* Function for creating the table of contents */

window.onload = function () {
	var toc = "";
	var level = 1;

	document.getElementsByTagName("body")[0].innerHTML =
		document.getElementsByTagName("body")[0].innerHTML.replace(
			/<h([\d])([^>]*)>([^<]+)<\/h([\d])>/gi,
			function (str, openhdr, attr, hdrtext, closehdr) {

				if (openhdr != closehdr || openhdr < 2
				    || hdrtext == "Index") {
					return str;
				}

				if (openhdr > level) {
					toc += (new Array(openhdr - level
							  + 1)).join("<ul>");
				} else if (openhdr < level) {
					toc += (new Array(level - openhdr
							  + 1)).join("</ul>");
				}

				level = parseInt(openhdr);

				var anchr = hdrtext.replace(/ /g, "_");
				toc += "<li><a href=\"#" + anchr + "\">"
					 + hdrtext + "</a></li>";

				return "<h"+openhdr + " id=\"" + anchr + "\" "
					+ attr + ">" + hdrtext
					+ "</h" + closehdr + ">";
			}
		);

	if (level) {
		toc += (new Array(level + 1)).join("</ul>");
	}

	document.getElementById("toc").innerHTML = toc;
};
