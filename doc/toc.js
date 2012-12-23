/** toc.js

    This is a simplified version of the scipt "generated_toc.js" written by:
            Stuart Langridge, July 2007

    The script is licensed under the terms of the MIT license.
    See the following page for details:
            http://www.kryogenix.org/code/browser/generated-toc/

    Generate a table of contents, based on headings in the page.

    To place the TOC on the page, add

    <div id="generated-toc"></div>

    to the page where you want the TOC to appear. If this element
    is not present, the TOC will not appear.

*/

generated_toc = {
  generate: function() {
    // Identify our TOC element, and what it applies to
    generate_from = '2';
    tocparent = document.getElementById('generated-toc');
    if (!tocparent) {
      // They didn't specify a TOC element; exit
      return;
    }

    // set top_node to be the element in the document under which
    // we'll be analysing headings
    top_node = document.getElementsByTagName('body')[0];

    // If there isn't a specified header level to generate from, work
    // out what the first header level inside top_node is
    // and make that the specified header level
    if (generate_from == 0) {
      first_header_found = generated_toc.findFirstHeader(top_node);
      if (!first_header_found) {
        // there were no headers at all inside top_node!
        return;
      } else {
        generate_from = first_header_found.toLowerCase().substr(1);
      }
    }

    // add all levels of heading we're paying attention to to the
    // headings_to_treat dictionary, ready to be filled in later
    headings_to_treat = {"h6":''};
    for (var i=5; i>= parseInt(generate_from); i--) {
      headings_to_treat["h" + i] = '';
    }

    // get headings. We can't say
    // getElementsByTagName("h1" or "h2" or "h3"), etc, so get all
    // elements and filter them ourselves
    // need to use .all here because IE doesn't support gEBTN('*')
    nodes = top_node.all ? top_node.all : top_node.getElementsByTagName('*');

    // put all the headings we care about in headings
    headings = [];
    for (var i=0; i<nodes.length;i++) {
      if (nodes[i].nodeName.toLowerCase() in headings_to_treat) {
        // if heading has class no-TOC, skip it
        if ((' ' + nodes[i].className + ' ').indexOf('no-TOC') != -1) {
          continue;
        }
        headings.push(nodes[i]);
      }
    }

    // make the basic elements of the TOC itself, ready to fill into
    cur_head_lvl = "h" + generate_from;
    cur_list_el = document.createElement('ul');
    tocparent.appendChild(cur_list_el);

    // now walk through our saved heading nodes
    for (var i=0; i<headings.length; i++) {
      this_head_el = headings[i];
      this_head_lvl = headings[i].nodeName.toLowerCase();
      if (!this_head_el.id) {
        // if heading doesn't have an ID, give it one
        //this_head_el.id = 'heading_toc_' + i;
        var id = generated_toc.innerText(this_head_el).replace(/[ \/]/g, "_");
        id = id.replace(/[^a-zA-Z0-9-_]/g, '');
        this_head_el.id = id;
      }

      while(this_head_lvl > cur_head_lvl) {
        // this heading is at a lower level than the last one;
        // create additional nested lists to put it at the right level

        // get the *last* LI in the current list, and add our new UL to it
        var last_listitem_el = null;
        for (var j=0; j<cur_list_el.childNodes.length; j++) {
          if (cur_list_el.childNodes[j].nodeName.toLowerCase() == 'li') {
            last_listitem_el = cur_list_el.childNodes[j];
          }
        }
        if (!last_listitem_el) {
          // there aren't any LIs, so create a new one to add the UL to
          last_listitem_el = document.createElement('li');
        }
        new_list_el = document.createElement('ul');
        last_listitem_el.appendChild(new_list_el);
        cur_list_el.appendChild(last_listitem_el);
        cur_list_el = new_list_el;
        cur_head_lvl = 'h' + (parseInt(cur_head_lvl.substr(1,1)) + 1);
      }

      while (this_head_lvl < cur_head_lvl) {
        // this heading is at a higher level than the last one;
        // go back up the TOC to put it at the right level
        cur_list_el = cur_list_el.parentNode.parentNode;
        cur_head_lvl = 'h' + (parseInt(cur_head_lvl.substr(1,1)) - 1);
      }

      // create a link to this heading, and add it to the TOC
      li = document.createElement('li');
      a = document.createElement('a');
      a.href = '#' + this_head_el.id;
      a.appendChild(document.createTextNode(generated_toc.innerText(this_head_el)));
      li.appendChild(a);
      cur_list_el.appendChild(li);
    }
  },

  innerText: function(el) {
    return (typeof(el.innerText) != 'undefined') ? el.innerText :
          (typeof(el.textContent) != 'undefined') ? el.textContent :
          el.innerHTML.replace(/<[^>]+>/g, '');
  },

  findFirstHeader: function(node) {
    // a recursive function which returns the first header it finds inside
    // node, or null if there are no functions inside node.
    var nn = node.nodeName.toLowerCase();
    if (nn.match(/^h[1-6]$/)) {
      // this node is itself a header; return our name
      return nn;
    } else {
      for (var i=0; i<node.childNodes.length; i++) {
        var subvalue = generated_toc.findFirstHeader(node.childNodes[i]);
        // if one of the subnodes finds a header, abort the loop and return it
        if (subvalue) return subvalue;
      }
      // no headers in this node at all
      return null;
    }
  },

  getYPos: function(el) {
    var y = 0;
    while (el && !isNaN(el.offsetTop)) {
        y += el.offsetTop;
        el = el.parentNode;
    }
    return y;
  },

  init: function() {
    // quit if this function has already been called
    if (arguments.callee.done) return;

    // flag this function so we don't do the same thing twice
    arguments.callee.done = true;

    generated_toc.generate();

    if (location.hash.length != 0) {
        // Make sure that the browser scrolled to the right location!
        var anchor = location.hash.substring(1);
        var y = generated_toc.getYPos(document.getElementById(anchor));
        if ('scrollTo' in window) {
            window.scrollTo(0, y);
        }
        else if ('scroll' in window) {
            window.scroll(0, y);
        }
    }
  }
};

/* Run generated_toc.init as soon as possible */
(function(i) {
  var u =navigator.userAgent;
  var e=/*@cc_on!@*/false;
  var st = setTimeout;
  if(/webkit/i.test(u)) {
    st(function() {
        var dr=document.readyState;
        if(dr=="loaded" || dr=="complete") {
          i()
        }
        else {
          st(arguments.callee,10);
        }
      },10
    );
  }
  else if((/mozilla/i.test(u) && !/(compati)/.test(u)) || (/opera/i.test(u))) {
    document.addEventListener("DOMContentLoaded",i,false);
  } else if(e) {
    (function() {
      var t=document.createElement('doc:rdy');
      try{
        t.doScroll('left');
        i();
        t=null;
      }
      catch(e) {
        st(arguments.callee,0);
      }
    })();
  }
  else{
    window.onload=i;
  }}
)(generated_toc.init);
