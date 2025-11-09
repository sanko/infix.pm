set $n = 100
while $n-- > 0
  printf "starting program\n"
  run
  if $_siginfo
    printf "Stopping!!\n"
    bt
    loop_break
  else
    printf "program exited\n"
  end
end
