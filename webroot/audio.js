(function (app) {
    var audioCtx = null;
    app.audio =
    {
        
        getContext: function (){
            if (!audioCtx) {
                var ac = (window.AudioContext || window.webkitAudioContext);
                if (!ac) return null;
                audioCtx = new ac();
            }
            return audioCtx;
        }

    };

})(app);