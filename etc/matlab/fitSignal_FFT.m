%from http://blog.accelereyes.com/blog/2012/09/10/time-delay-estimation-algorithms-with-jacket/


% ******************************************************************************
% delay-matching between two signals (complex/real-valued) M. Nentwig
%
% * matches the continuous-time equivalent waveforms of the signal vectors
% (reconstruction at Nyquist limit => ideal lowpass filter)
% * Signals are considered cyclic. Use arbitrary-length zero-padding to turn a
% one-shot signal into a cyclic one.
%
% * output:
%   => coeff: complex scaling factor that scales 'ref' into 'signal'
%   => delay 'deltaN' in units of samples (subsample resolution)
%      apply both to minimize the least-square residual
%   => 'shiftedRef': a shifted and scaled version of 'ref' that
%      matches 'signal'
%   => (signal - shiftedRef) gives the residual (vector error)
%
% ******************************************************************************
function [Xcor, deltaN] = fitSignal_FFT(signal, ref)
    n = length(signal);


    % Delay calculation starts: Convert to frequency domain...
    sig_FD = fft(signal); ref_FD = fft(ref, n);

    % ... calculate crosscorrelation between signal and reference...
    u=sig_FD .* conj(ref_FD);
    if mod(n, 2) == 0
        % for an even sized FFT the center bin represents a signal
        % [-1 1 -1 1 ...] (subject to interpretation). It cannot be delayed.
        % The frequency component is therefore excluded from the calculation.
        u(length(u)/2 + 1)=0;
    end
    Xcor=(ifft(u));

    % Each bin in Xcor corresponds to a given delay in samples. The bin with the
    % highest absolute value corresponds to the delay where maximum correlation
    % occurs.
    integerDelay = find((Xcor==max(Xcor)));
    
    deltaN = integerDelay;
end