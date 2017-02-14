        var updateNodeElements = function () {
            var nodes = $('#nodes');
            var n = nodes.children().length;
            var w = Math.max(document.documentElement.clientWidth, window.innerWidth || 0)
            var h = Math.max(document.documentElement.clientHeight, window.innerHeight || 0)

            var r = Math.min(w, h) * 0.45 - 150;
            nodes.children().each(function (i) {
                //console.log(i);
                var phi = i / n * Math.PI * 2;
                var nodeR = this.offsetWidth;
                var dns = $(this).children('.audio-device');
                var nd = dns.length;
                $(this)
                    .css('margin-top', (Math.sin(phi) * r) + 'px')
                    .css('margin-left', (Math.cos(phi) * r) + 'px');

                dns.each(function (di) {
                    var phi = di / nd * Math.PI * 2;
                    var nameEl = $(this).find('.name');
                    var s = Math.sin(phi);
                    nameEl
                        .css('margin-top', (Math.sign(s) * Math.pow(Math.abs(s), 1.5) * (nodeR + nameEl[0].offsetHeight / 2)) + 'px')
                        .css('margin-left', (Math.cos(phi) * nodeR - nameEl[0].offsetWidth / 2 + this.offsetWidth / 2) + 'px');

                    $(this).find('.connector')
                        .css('top', (Math.sin(phi) * nodeR * 1.5) + 'px')
                        .css('left', (Math.cos(phi) * nodeR * 1.5) + 'px');

                });
            });

        };

                window.onresize = function (event) {
            updateNodeElements();
        };
