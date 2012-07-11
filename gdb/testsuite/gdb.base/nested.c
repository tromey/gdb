typedef int compute_function (int);

int nestee (compute_function *computer, int arg, int self_call)
{
  int nested (int nested_arg)
  {
    return nested_arg + 23 + self_call;	/* Break here */
  }

  if (self_call)
    arg = nestee (nested, arg + 5, 0);

  return computer (arg);
}

int misc (int arg)
{
  return 0;
}

int main(int argc, char **argv)
{
  nestee (misc, 5, 1);
  return 0;
}
