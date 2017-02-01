function [ deltaN, cor ] = find_delay( sig, ref )
 
    %ref = ref / (max(abs(ref)) + 0.001);
    %sig = sig / (max(abs(sig)) + 0.001);
    
    medLen=11;
    %ref = ref - medfilt1(ref,medLen);    
    %sig = sig - medfilt1(sig,medLen);
    
  %  ref = abs(ref);
  %  sig = abs(sig);

    [cor, deltaN] = fitSignal_FFT(sig, ref);
    
    
    return;
   
end

